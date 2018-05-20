#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string>

#include "../asynclog.hpp"
#include "../sinks/fmt_sink.hpp"


namespace tz { namespace asynclog {

    struct FileSink : FormatterSink {
        explicit FileSink(const std::string &path)
            : path(path), fp(NULL)
        {
            ::memset(&this->file_stat, 0, sizeof(this->file_stat));
        }

        ~FileSink() {
            this->close();
        }

        virtual bool sink(LogMsg *msg) {
            bool ok = true;
            if (this->fp == NULL) {
                if (!this->reload()) {
                    this->logger->_internal_log(ALOG_LVL_FATAL, "reload failed");
                    ok = false;
                    goto L_RETURN;
                }
            }

            {
                std::string &buf = this->fmtbuf;
                buf.clear();
                this->format(buf, msg);
                buf.push_back('\n');

                size_t n = fwrite(buf.data(), 1, buf.size(), this->fp);
                if (n != buf.size()) {
                    this->logger->_internal_log(ALOG_LVL_ERROR, "fwrite() error");
                    ok = false;
                    goto L_RETURN;
                }
            }

        L_RETURN:
            this->logger->recycle(msg);
            return ok;
        }

        virtual void flush() {
            if (this->fp != NULL) {
                if (fflush(this->fp) != 0) {
                    this->logger->_internal_log(ALOG_LVL_ERROR, "fflush() failed. [errno:%d][path:%s]",
                        errno, this->path.c_str());
                }
                // check rotate
                if (!this->reload()) {
                    this->logger->_internal_log(ALOG_LVL_FATAL, "reload failed");
                }
            }
        }

        virtual void close() {
            if (this->fp != NULL) {
                if (fclose(this->fp) != 0) {
                    this->logger->_internal_log(ALOG_LVL_ERROR, "close() failed. [path:%s]", this->path.c_str());
                }
                this->fp = NULL;
            }
        }

        bool reload() {
            if (this->fp == NULL) {
                // TODO: check file directory
                this->logger->_internal_log(ALOG_LVL_INFO, "fp is NULL, open log file. [path:%s]", this->path.c_str());
                this->fp = fopen(this->path.c_str(), "a");
                if (this->fp == NULL) {
                    this->logger->_internal_log(ALOG_LVL_FATAL, "open log file failed. [path:%s]", this->path.c_str());
                    return false;
                }

                if (0 != ::stat(this->path.c_str(), &this->file_stat)) {
                    this->logger->_internal_log(ALOG_LVL_ERROR, "stat() failed. [path:%s]", this->path.c_str());
                    return false;
                }
            } else {
                bool found = false;
                struct stat cur_stat;
                if (0 != ::stat(this->path.c_str(), &cur_stat)) {
                    if (errno != ENOENT) {
                        this->logger->_internal_log(ALOG_LVL_ERROR, "stat() failed. [path:%s]", this->path.c_str());
                        return false;
                    }
                } else {
                    found = true;
                }

                // path is disappeared or points to another file
                if (!found
                    || this->file_stat.st_dev != cur_stat.st_dev
                    || this->file_stat.st_ino != cur_stat.st_ino)
                {
                    this->logger->_internal_log(ALOG_LVL_INFO, "log file changed, reopen file. [path:%s]", this->path.c_str());
                    this->close();
                    return this->reload();
                }
            }

            return true;
        }

        std::string path;
        FILE *fp;
        struct stat file_stat;
        std::string fmtbuf;
    };

}}  // ::tz::asynclog
