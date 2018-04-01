#pragma once

#include <boost/shared_ptr.hpp>

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

        void set_formatter(const boost::shared_ptr<IFormatter> &formatter) {
            this->formatter = formatter;
        }

        boost::shared_ptr<IFormatter> formatter;
    };

}}  // ::tz::asynclog
