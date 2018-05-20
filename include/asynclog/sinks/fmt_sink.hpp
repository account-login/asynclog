#pragma once

#include "../asynclog.hpp"
#include "../formatter.hpp"


namespace tz { namespace asynclog {

    struct FormatterSink : ILogSink {
        FormatterSink()
            : formatter(new DefaultFormtter())
        {}

        void format(std::string &buf, LogMsg *msg) {
            assert(this->formatter.get() != NULL);
            this->formatter->format(buf, msg);
        }

        void set_formatter(const TZ_ASYNCLOG_SHARED_PTR<IFormatter> &formatter) {
            this->formatter = formatter;
        }

        TZ_ASYNCLOG_SHARED_PTR<IFormatter> formatter;
    };

}}  // ::tz::asynclog
