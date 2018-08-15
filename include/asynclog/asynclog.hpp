#pragma once

#include <stdint.h>
#include <string.h>
#include <time.h>       // for nanosleep
#include <sched.h>      // for sched_yield
#include <stdio.h>      // for snprintf, stderr
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>  // for pid_t and etc
#include <sys/time.h>   // for gettimeofday
#if  __linux__
#   include <sys/syscall.h>
#endif
#include <cassert>
#include <string>

#include <turf/Atomic.h>

#ifdef TZ_ASYNCLOG_USE_STB_SPRINTF
#   include <stb_sprintf.h>
#   define TZ_ASYNCLOG_VSNPRINTF stbsp_vsnprintf
#   define TZ_ASYNCLOG_SNPRINTF  stbsp_snprintf
#else
#   define TZ_ASYNCLOG_VSNPRINTF vsnprintf
#   define TZ_ASYNCLOG_SNPRINTF  snprintf
#endif

#include "helper.hpp"
#include "concurrency.hpp"
#include "queue.hpp"


// TODO: signal handler


namespace tz { namespace asynclog {

#define TZ_ASYNCLOG_MAX_LEN 2048

    enum LogLevel {
        ALOG_LVL_DEBUG  = 1,
        ALOG_LVL_INFO   = 2,
        ALOG_LVL_NOTICE = 3,
        ALOG_LVL_WARN   = 4,
        ALOG_LVL_ERROR  = 5,
        ALOG_LVL_FATAL  = 6,
        ALOG_LVL_MAX,
    };

    typedef uint8_t LevelType;

    enum MsgType {
        MSGTYPE_LOG = 0,
        MSGTYPE_STOP,
        MSGTYPE_FLUSH,
    };

    struct LogMsg {
        uint8_t         type;   // MsgType
        LevelType       level;
        struct timeval  time;
        pid_t           tid;
        // TODO: source, lineno and etc
        size_t          msg_size;
        char            msg_data[0];
    };

    struct AsyncLogger;

    struct ILogSink {
        typedef TZ_ASYNCLOG_SHARED_PTR<ILogSink> Ptr;

        AsyncLogger *logger;

        ILogSink() : logger(NULL) {}
        virtual ~ILogSink() {}
        virtual bool sink(LogMsg *msg) = 0;
        virtual void flush() = 0;
        virtual void close() = 0;
    };

    struct IFormatter {
        typedef TZ_ASYNCLOG_SHARED_PTR<IFormatter> Ptr;

        virtual ~IFormatter() {}
        virtual void format(std::string &buf, LogMsg *msg) = 0;
    };

    struct AsyncLogger {
        explicit AsyncLogger(size_t queue_size)
            : psink()
            , q(queue_size)     // TODO: wrap queue size
            , level(ALOG_LVL_DEBUG)
            , stopped(false)
            , internal_logfile(NULL)
            , flush_interval_ms(200)
            , format_buffer_size(TZ_ASYNCLOG_MAX_LEN)
        {}

        ~AsyncLogger();

        // public
        AsyncLogger &set_sink(const ILogSink::Ptr &sink) {
            this->psink = sink;
            this->psink->logger = this;
            return *this;
        }
        AsyncLogger &set_queue_size(size_t size) {
            // TODO: wrap queue size
            this->q.reset(size);
            return *this;
        }
        AsyncLogger &set_level(LevelType level);

        void log(LevelType level, const char *fmt, ...);
        void vlog(LevelType level, const char *fmt, va_list ap);
        void binlog(LevelType level, const char *data, size_t size);
        bool sink(LogMsg *msg);
        void flush();
        void recycle(LogMsg *msg);
        LogMsg *create(size_t msg_size);
        bool should_log(LevelType level);
        void start();
        void stop();    // FIXME: not thread safe

        // private
        static void *_consumer(void *arg);
        void _internal_log(LevelType level, const char *fmt, ...);

        static pid_t get_tid();

        // private
        ILogSink::Ptr psink;
        MPMCBoundedQueue<LogMsg *> q;
        turf::Atomic<LevelType> level;
        TZ_ASYNCLOG_SHARED_PTR<_Thread> consumer_thread;
        bool stopped;

        FILE *internal_logfile;

        struct Stats {
            turf::Atomic<uint64_t> total;
            turf::Atomic<uint64_t> drop;
            turf::Atomic<uint64_t> err;
            turf::Atomic<uint64_t> trunc;

            Stats()
                : total(0), drop(0), err(0), trunc(0)
            {}
        } stats;

        // params
        // TODO: add to config
        uint32_t flush_interval_ms;
        uint32_t format_buffer_size;

        // no copy
    private:
        AsyncLogger &operator=(const AsyncLogger &other);
        AsyncLogger(const AsyncLogger &other);
    };

    // for syslog hook
    AsyncLogger &init_global_logger(size_t queue_size);     // not thread safe
    AsyncLogger &get_global_logger(void);

    // <impl>

    inline pid_t AsyncLogger::get_tid() {
        static __thread pid_t tid;  // zero initialized
        if (tid == 0) {
#ifdef SYS_gettid
            tid = ::syscall(SYS_gettid);
#else
            tid = ::getpid();       // tid unavailable, use pid instead
#endif
        }
        return tid;
    }

    inline void AsyncLogger::stop() {
        if (this->stopped) {
            return;
        }

        LogMsg *msg = new LogMsg();     // special msg type do not go through pool
        msg->type = MSGTYPE_STOP;
        while (!this->q.try_push_back(msg)) {}
        this->consumer_thread->join();
        this->stopped = true;

        if (this->internal_logfile && this->internal_logfile != stderr) {
            fclose(this->internal_logfile);     // ignore err
            this->internal_logfile = NULL;
        }
    }

    inline AsyncLogger::~AsyncLogger() {
        this->stop();
    }

    inline bool AsyncLogger::sink(LogMsg *msg) {
        assert(msg != NULL);
        return this->q.try_push_back(msg);
    }

    inline void AsyncLogger::flush() {
        LogMsg *msg = new LogMsg();
        msg->type = MSGTYPE_FLUSH;
        while (!this->q.try_push_back(msg)) {}
    }

    inline void AsyncLogger::recycle(LogMsg *msg) {
        assert(msg != NULL);
        ::free(msg);
    }

    inline LogMsg *AsyncLogger::create(size_t msg_size) {
        return (LogMsg *)::malloc(sizeof(LogMsg) + msg_size);
    }

    inline AsyncLogger &AsyncLogger::set_level(LevelType level) {
        this->level.store(level, turf::Relaxed);
        return *this;
    }

    inline bool AsyncLogger::should_log(LevelType level) {
        return level >= this->level.load(turf::Relaxed);
    }

    inline void AsyncLogger::start() {
        assert(this->psink.get() != NULL);
        assert(this->consumer_thread.get() == NULL);
        assert(this->internal_logfile == NULL);

        if (::getenv("ALOG_INTERNAL_LOG_STDERR")) {
            this->internal_logfile = stderr;
        } else if (const char *filename = ::getenv("ALOG_INTERNAL_LOG_FILE")) {
            this->internal_logfile = fopen(filename, "at+");    // ignore err
            // set line buffer
            setvbuf(this->internal_logfile, NULL, _IOLBF, BUFSIZ);
        }

        this->consumer_thread.reset(new _Thread(&AsyncLogger::_consumer, this));
    }

    inline bool _wait_a_moment(size_t attempts) {
        // TODO: adjust this
        const size_t k_spin_threshold = 10;
        const size_t k_yield_threshold = 100;
        const size_t k_max_sleep_us_log2 = 13;
        const size_t k_max_sleep_us = 8 * 1024;     // 8ms

        if (attempts < k_spin_threshold) {
            // spin
            return false;
        } else if (attempts < k_yield_threshold) {
            ::sched_yield();
            return false;
        } else {
            uint64_t us;
            if (attempts - k_yield_threshold > k_max_sleep_us_log2) {
                us = k_max_sleep_us;
            } else {
                us = 1llu << (attempts - k_yield_threshold);
            }
            timespec ts = {0, (long)us * 1000};
            ::nanosleep(&ts, NULL);
            return true;
        }
    }

    inline uint64_t _timeval_to_msec(const struct timeval &tv) {
        return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    inline uint64_t _get_time_msec() {
        struct timeval tv;
        ::gettimeofday(&tv, NULL);
        return _timeval_to_msec(tv);
    }

    inline void *AsyncLogger::_consumer(void *arg) {
        AsyncLogger *logger = (AsyncLogger *)arg;
        ILogSink *sink = logger->psink.get();
        assert(sink != NULL);

        size_t attempts = 0;
        uint64_t last_flush = _get_time_msec();
        while (true) {
            LogMsg *msg = NULL;
            if (!logger->q.try_pop_front(msg)) {
                // queue empty, wait a moment
                bool sleeped = _wait_a_moment(++attempts);
                if (sleeped) {
                    // check for flush
                    uint64_t now = _get_time_msec();
                    if (now >= last_flush + logger->flush_interval_ms) {
                        last_flush = now;
                        sink->flush();
                    }
                }
                // retry
                continue;
            }

            attempts = 0;
            switch (msg->type) {
            case MSGTYPE_STOP:
                delete msg;
                sink->flush();
                sink->close();
                return NULL;    // thread exit
            case MSGTYPE_FLUSH:
                delete msg;
                last_flush = _get_time_msec();
                sink->flush();
                break;
            case MSGTYPE_LOG:
                // check for flush before msg is deleted
                if (_timeval_to_msec(msg->time) >= last_flush + logger->flush_interval_ms) {
                    last_flush = _timeval_to_msec(msg->time);
                    sink->sink(msg);    // msg moved to sink
                    sink->flush();
                } else {
                    sink->sink(msg);
                }
                break;
            default:
                assert(!"unknown MSGTYPE");
            }
        }   // while true
    }

    inline const char *_internal_log_level_string(LevelType level) {
        switch (level) {
#define _INTERNAL_LOG_LEVEL_CASE(lvl) case lvl: return #lvl
        _INTERNAL_LOG_LEVEL_CASE(ALOG_LVL_DEBUG);
        _INTERNAL_LOG_LEVEL_CASE(ALOG_LVL_INFO);
        _INTERNAL_LOG_LEVEL_CASE(ALOG_LVL_NOTICE);
        _INTERNAL_LOG_LEVEL_CASE(ALOG_LVL_WARN);
        _INTERNAL_LOG_LEVEL_CASE(ALOG_LVL_ERROR);
        _INTERNAL_LOG_LEVEL_CASE(ALOG_LVL_FATAL);
#undef _INTERNAL_LOG_LEVEL_CASE
        default: return "ALOG_LVL_UNKNOWN";
        }
    }

    inline void AsyncLogger::_internal_log(LevelType level, const char *fmt, ...) {
        if (this->internal_logfile == NULL) {
            return;
        }

        time_t now = ::time(NULL);
        struct tm stm;
        localtime_r(&now, &stm);
        char timebuf[4 + 1 + 2 + 1 + 2 + 3 * 3 + 1] = {};
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &stm);

        fprintf(this->internal_logfile, "%s [AsyncLogger::_internal_log] %s\t",
            timebuf, _internal_log_level_string(level));
        va_list ap;
        va_start(ap, fmt);
        vfprintf(this->internal_logfile, fmt, ap);
        va_end(ap);
        fputc('\n', this->internal_logfile);
    }

    inline void AsyncLogger::log(LevelType level, const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        this->vlog(level, fmt, ap);
        va_end(ap);
    }

    inline void AsyncLogger::vlog(LevelType level, const char *fmt, va_list ap) {
        char buf[this->format_buffer_size];     // NOTE: stack overflow
        int n = TZ_ASYNCLOG_VSNPRINTF(buf, sizeof(buf), fmt, ap);

        const char *msgdata = NULL;
        size_t msgsize = 0;
        if (n > 0) {
            msgdata = buf;
            msgsize = (size_t)n;
            if (msgsize >= sizeof(buf)) {
                msgsize = sizeof(buf) - 1;
                this->stats.trunc.fetchAdd(1, turf::Relaxed);
            }
        } else {
            this->stats.err.fetchAdd(1, turf::Relaxed);
            const char *errmsg = "[AsyncLogger] bad vsnprintf call";
            msgdata = errmsg;
            msgsize = strlen(errmsg);
        }

        return this->binlog(level, msgdata, msgsize);
    }

    inline void AsyncLogger::binlog(LevelType level, const char *data, size_t size) {
        LogMsg *msg = this->create(size);
        msg->type = MSGTYPE_LOG;
        msg->level = level;
        ::gettimeofday(&msg->time, NULL);
        msg->tid = this->get_tid();
        ::memcpy(msg->msg_data, data, size);
        msg->msg_size = size;

        if (!this->sink(msg)) {
            this->recycle(msg);
            this->stats.drop.fetchAdd(1, turf::Relaxed);
        }
        this->stats.total.fetchAdd(1, turf::Relaxed);
    }

#define TZ_ASYNC_LOG(logger, level, fmt, ...) \
    do { \
        if (logger.should_log(level)) { \
            logger.log(level, fmt, ##__VA_ARGS__); \
        } \
    } while (false)


}}  // ::tz::asynclog
