#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

    namespace {
        const size_t k_buf_size = 4096;
    }

    struct FileSink : FormatterSink {
        explicit FileSink(const std::string &path)
            : path(path), fd(-1), bufidx(0)
        {
            ::memset(&this->file_stat, 0, sizeof(this->file_stat));
        }

        ~FileSink() {
            this->close();
        }

        virtual bool sink(LogMsg *msg) {
            bool ok = true;
            if (this->fd < 0) {
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

                if (!this->_write(buf.data(), buf.size())) {
                    goto L_RETURN;
                }
            }

        L_RETURN:
            this->logger->recycle(msg);
            return ok;
        }

        bool _write(const char *data, size_t size) {
            if (size + this->bufidx > k_buf_size) {
                if (!this->_flush()) {
                    return false;
                }
            }

            if (size >= k_buf_size) {
                // log too long, write without go through buffer
                ssize_t rv = ::write(this->fd, data, size);
                if (rv != (ssize_t)size) {
                    this->logger->_internal_log(ALOG_LVL_FATAL, "write() error [fd:%d][errno:%d]", this->fd, errno);
                    return false;
                }
            } else {
                // append log to buffer
                ::memcpy(&this->buf[this->bufidx], data, size);
                this->bufidx += size;
            }

            return true;
        }

        bool _flush() {
            ssize_t rv = ::write(this->fd, this->buf, this->bufidx);
            if (rv != (ssize_t)this->bufidx) {
                this->logger->_internal_log(ALOG_LVL_FATAL, "[_flush] write() error [fd:%d][errno:%d]", this->fd, errno);
                return false;
            }

            this->bufidx = 0;
            return true;
        }

        virtual void flush() {
            if (this->fd >= 0) {
                if (!this->_flush()) {
                    return;
                }

                // check rotate
                if (!this->reload()) {
                    this->logger->_internal_log(ALOG_LVL_FATAL, "reload failed");
                }
            }
        }

        virtual void close() {
            if (this->fd >= 0) {
                if (::close(this->fd) != 0) {
                    this->logger->_internal_log(ALOG_LVL_ERROR, "close() failed. [errno:%d][path:%s]",
                        errno, this->path.c_str());
                }
                this->fd = -1;
            }
        }

        bool reload() {
            if (this->fd < 0) {
                this->logger->_internal_log(ALOG_LVL_INFO, "fd < 0, open log file. [path:%s]", this->path.c_str());
                this->fd = ::open(this->path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
                if (this->fd < 0) {
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
                    this->fd = ::open(this->path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
                    if (this->fd < 0) {
                        this->logger->_internal_log(ALOG_LVL_FATAL, "open log file again failed. [errno:%d][path:%s]",
                            errno, this->path.c_str());
                        return false;
                    }
                }

                this->logger->_internal_log(ALOG_LVL_INFO, "opened log file [fd:%d][path:%s]", this->fd, this->path.c_str());

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
        int fd;
        char buf[k_buf_size];
        size_t bufidx;
        struct stat file_stat;
        std::string fmtbuf;
    };

}}  // ::tz::asynclog
