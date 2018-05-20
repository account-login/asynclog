#include <syslog.h>
#include <memory>


#include "asynclog/asynclog.hpp"
#include "asynclog/sinks/file_sink.hpp"
#include "asynclog/helper.hpp"


#define TZ_ASYNCLOG_HOOK_LOGGER_QUEUE_SIZE (1024 * 1024)


namespace tz { namespace asynclog {

    static std::auto_ptr<tz::asynclog::AsyncLogger> g_logger;

    AsyncLogger &init_global_logger(size_t queue_size) {
        g_logger.reset(new AsyncLogger(queue_size));
        g_logger->set_sink(ILogSink::Ptr(new FileSink(_get_process_name() + ".log")));
        return *g_logger;
    }

    AsyncLogger &get_global_logger(void) {
        return *g_logger;
    }

}}  // ::tz::asynclog


// FIXME: not thread safe
void closelog(void) {
    tz::asynclog::get_global_logger().stop();
}

// FIXME: not thread safe
void openlog(const char *ident, int option, int facility) {
    (void)ident;
    (void)option;
    (void)facility;
    // TODO: config logger from config file
    tz::asynclog::AsyncLogger &logger
        = tz::asynclog::init_global_logger(TZ_ASYNCLOG_HOOK_LOGGER_QUEUE_SIZE);
    logger.start();
}

int setlogmask(int mask) {
    (void)mask;
    // TODO: ...
    return LOG_UPTO(LOG_DEBUG);
}

static tz::asynclog::LevelType translate_priority(int priority) {
    switch (priority) {
    case LOG_EMERG:     return tz::asynclog::ALOG_LVL_FATAL;
    case LOG_ALERT:     return tz::asynclog::ALOG_LVL_FATAL;
    case LOG_CRIT:      return tz::asynclog::ALOG_LVL_FATAL;
    case LOG_ERR:       return tz::asynclog::ALOG_LVL_ERROR;
    case LOG_WARNING:   return tz::asynclog::ALOG_LVL_WARN;
    case LOG_NOTICE:    return tz::asynclog::ALOG_LVL_NOTICE;
    case LOG_INFO:      return tz::asynclog::ALOG_LVL_INFO;
    case LOG_DEBUG:     return tz::asynclog::ALOG_LVL_DEBUG;
    default:            return tz::asynclog::ALOG_LVL_FATAL;
    }
}

void syslog(int priority, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    tz::asynclog::get_global_logger().vlog(translate_priority(priority), format, ap);
    va_end(ap);
}

void vsyslog(int priority, const char *format, va_list ap) {
    tz::asynclog::get_global_logger().vlog(translate_priority(priority), format, ap);
}
