#pragma once

#include <stdio.h>
#include <string>

#include "fmt_sink.hpp"


namespace tz { namespace asynclog {

    struct FpSink : FormatterSink {
        explicit FpSink(FILE *fp)
            : fp(fp)
        {}

        virtual bool sink(LogMsg *msg) {
            std::string buf;
            this->format(buf, msg);
            buf.push_back('\n');

            size_t n = fwrite(buf.data(), 1, buf.size(), this->fp);
            if (n != buf.size()) {
                this->logger->_internal_log(ALOG_LVL_FATAL,
                    "fwrite() error [size:%zu][writen:%zu][errno:%d]", buf.size(), n, errno);
            }

            this->logger->recycle(msg);
            return true;
        }

        virtual void flush() {
            int rv = fflush(this->fp);
            if (rv != 0) {
                this->logger->_internal_log(ALOG_LVL_FATAL, "fflush() error [errno:%d]", errno);
            }
        }

        virtual void close() {
            // this->fp is not managed by this
        }

        // private
        FILE *fp;
    };

}}  // ::tz::asynclog
