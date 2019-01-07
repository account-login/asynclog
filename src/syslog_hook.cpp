// system
#include <dlfcn.h>      // for dladdr
#include <syslog.h>
#include <pthread.h>    // for pthread_once
#include <string>
#include <vector>
#include <memory>
// proj
#include "asynclog/asynclog.hpp"
#include "asynclog/sinks/file_sink.hpp"
#include "asynclog/helper.hpp"
#include "asynclog/config.hpp"


#define TZ_ASYNCLOG_HOOK_LOGGER_QUEUE_SIZE (1024 * 1024)


using namespace std;

namespace tz { namespace asynclog {

    static std::auto_ptr<tz::asynclog::AsyncLogger> g_logger;
    static pthread_once_t g_openlog_once_ctrl = PTHREAD_ONCE_INIT;

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

static const char *get_so_path_by_sym(void *sym) {
    Dl_info dl_info;
    ::memset(&dl_info, 0x00, sizeof(dl_info));
    int rv = ::dladdr(sym, &dl_info);
    if (rv != 0 && dl_info.dli_fname) {
        return dl_info.dli_fname;
    }
    return NULL;
}

static void reverse(char *buf, size_t size) {
    for (size_t i = 0; i < size / 2; ++i) {
        size_t j = size - i - 1;
        char t = buf[i];
        buf[i] = buf[j];
        buf[j] = t;
    }
}

static void openlog_once() {
    vector<string> config_file_list;
    if (const char *configfile = ::getenv("ALOG_CONFIG_FILE")) {
        config_file_list.push_back(configfile);
    }
    config_file_list.push_back("../conf/asynclog.json");
    config_file_list.push_back("asynclog.json");

    tz::asynclog::AsyncLogger &logger
        = tz::asynclog::init_global_logger(TZ_ASYNCLOG_HOOK_LOGGER_QUEUE_SIZE);

    std::string config_file;
    std::string errmsg = "no config file found";
    bool ok = false;
    size_t i = 0;
    for (i = 0; i < config_file_list.size(); ++i) {
        config_file = config_file_list[i];
        struct stat st;
        int rv = ::stat(config_file.c_str(), &st);
        if (rv == 0) {
            ok = tz::asynclog::config_logger_from_file(logger, config_file, errmsg);
            break;
        } else {
            logger._internal_log(tz::asynclog::ALOG_LVL_WARN,
                "stat() failed. [errno:%d] skip config file: %s", errno, config_file.c_str());
        }
    }

    if (i == config_file_list.size()) {
        // no config file found, get embedded config
        if (const char *sopath = get_so_path_by_sym((void *)openlog)) {
            if (FILE *sofile = ::fopen(sopath, "rb")) {
                // read last 1k data
                const int k_data_size = 1024;
                char buf[k_data_size + 1] = {};

                int rv = ::fseek(sofile, -k_data_size, SEEK_END);
                if (rv != 0) {
                    goto L_DONE;
                }

                if (::fread(buf, k_data_size, 1, sofile) == 1) {
                    // extract config json
                    const char *sig_rev = ">GOLCNYSA<";     // <ASYNCLOG>
                    reverse(buf, k_data_size);

                    const char *needle = ::strstr(buf, sig_rev);
                    if (needle) {
                        assert(needle > buf && needle - buf < k_data_size);
                        size_t conf_size = needle - buf;
                        reverse(buf, conf_size);
                        string conf_str(buf, conf_size);

                        config_file = sopath;
                        ok = tz::asynclog::config_logger_from_string(logger, conf_str, errmsg);
                    }
                }

            L_DONE:
                ::fclose(sofile);
            }
        }
    }

    logger.start();
    if (!ok) {
        logger._internal_log(tz::asynclog::ALOG_LVL_ERROR, "can load config file: %s", errmsg.c_str());
    } else {
        logger._internal_log(tz::asynclog::ALOG_LVL_INFO, "loaded config file: %s", config_file.c_str());
    }
}

void openlog(const char *ident, int option, int facility) {
    (void)ident;
    (void)option;
    (void)facility;
    ::pthread_once(&tz::asynclog::g_openlog_once_ctrl, openlog_once);
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

void vsyslog(int priority, const char *format, va_list ap) {
    ::pthread_once(&tz::asynclog::g_openlog_once_ctrl, openlog_once);

    tz::asynclog::AsyncLogger &logger = tz::asynclog::get_global_logger();
    tz::asynclog::LevelType level = translate_priority(priority);
    if (logger.should_log(level)) {
        logger.vlog(level, format, ap);
    }
}

void syslog(int priority, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}

extern "C" void __syslog_chk(int priority, int flag, const char *format, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}
