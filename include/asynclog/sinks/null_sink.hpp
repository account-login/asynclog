#pragma once

#include "../asynclog.hpp"


namespace tz { namespace asynclog {

    struct NullSink : ILogSink {
        NullSink() {}

        virtual bool sink(LogMsg *msg) {
            this->logger->recycle(msg);
            return true;
        }

        virtual void flush() {}
        virtual void close() {}
    };

}}  // ::tz::asynclog
