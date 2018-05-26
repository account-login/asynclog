#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <vector>

#include "../asynclog.hpp"
#include "../sinks/fmt_sink.hpp"


namespace tz { namespace asynclog {

    inline bool _mkdir_recursive(std::string path, mode_t mode) {
        if (*path.rbegin() != '/') {
            path.push_back('/');
        }
        for (size_t i = 1; i < path.size(); ++i) {
            if (path[i] == '/') {
                path[i] = '\0';
                int rc = ::mkdir(path.c_str(), mode);
                if (rc != 0 && errno != EEXIST) {
                    return false;
                }
                path[i] = '/';
            }
        }
        return true;
    }

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
                    this->logger->_internal_log(ALOG_LVL_FATAL, "fwrite() error [errno:%d]", errno);
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
                    this->logger->_internal_log(ALOG_LVL_ERROR, "close() failed. [errno:%d][path:%s]",
                        errno, this->path.c_str());
                }
                this->fp = NULL;
            }
        }

        bool reload() {
            if (this->fp == NULL) {
                this->logger->_internal_log(ALOG_LVL_INFO, "fp is NULL, open log file. [path:%s]", this->path.c_str());
                this->fp = fopen(this->path.c_str(), "a");
                if (this->fp == NULL) {
                    // open log file failed
                    this->logger->_internal_log(ALOG_LVL_ERROR, "open log file failed. [errno:%d][path:%s]",
                        errno, this->path.c_str());

                    // check for missing dir
                    std::vector<char> buf(this->path.begin(), this->path.end());
                    if (!_mkdir_recursive(::dirname(buf.data()), 0755)) {
                        this->logger->_internal_log(ALOG_LVL_FATAL, "mkdir failed");
                        return false;
                    }
                    this->logger->_internal_log(ALOG_LVL_INFO, "created log dir");

                    // open file again
                    this->fp = fopen(this->path.c_str(), "a");
                    if (this->fp == NULL) {
                        this->logger->_internal_log(ALOG_LVL_FATAL, "open log file again failed. [errno:%d][path:%s]",
                            errno, this->path.c_str());
                        return false;
                    }
                }

                if (0 != ::stat(this->path.c_str(), &this->file_stat)) {
                    this->logger->_internal_log(ALOG_LVL_ERROR, "stat() failed. [errno:%d][path:%s]",
                        errno, this->path.c_str());
                    return false;
                }
            } else {
                bool found = false;
                struct stat cur_stat;
                if (0 != ::stat(this->path.c_str(), &cur_stat)) {
                    if (errno != ENOENT) {
                        this->logger->_internal_log(ALOG_LVL_ERROR, "stat() failed. [errno:%d][path:%s]",
                            errno, this->path.c_str());
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
