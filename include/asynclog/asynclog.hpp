#pragma once

#include <stdint.h>
#include <time.h>       // for nanosleep
#include <sched.h>      // for sched_yield
#include <stdio.h>      // for snprintf, stderr
#include <stdarg.h>
#include <sys/types.h>  // for pid_t and etc
#include <sys/time.h>   // for gettimeofday
#include <sys/syscall.h>
#include <cassert>
#include <string>

// TODO: get rid of boost
#include <boost/thread/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/bind.hpp>
#include <turf/Atomic.h>

#ifdef TZ_ASYNCLOG_USE_STB_SPRINTF
#   include <stb_sprintf.h>
#   define TZ_ASYNCLOG_VSNPRINTF stbsp_vsnprintf
#   define TZ_ASYNCLOG_SNPRINTF  stbsp_snprintf
#else
#   define TZ_ASYNCLOG_VSNPRINTF vsnprintf
#   define TZ_ASYNCLOG_SNPRINTF  snprintf
#endif

#include "queue.hpp"


namespace tz { namespace asynclog {

#define TZ_ASYNCLOG_MAX_LEN 2048

    enum LogLevel {
        ALOG_LVL_DEBUG,
        ALOG_LVL_INFO,
        ALOG_LVL_NOTICE,
        ALOG_LVL_WARN,
        ALOG_LVL_ERROR,
        ALOG_LVL_FATAL,
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
        // TODO: embbed msg into struct
        std::string     msg;
        // size_t          msg_size;
        // char            msg_data[TZ_ASYNCLOG_MAX_LEN];
    };

    struct AsyncLogger;

    struct ILogSink {
        typedef boost::shared_ptr<ILogSink> Ptr;

        AsyncLogger *logger;

        ILogSink() : logger(NULL) {}
        virtual ~ILogSink() {}
        virtual bool sink(LogMsg *msg) = 0;
        virtual void flush() = 0;
        virtual void close() = 0;
    };

    struct IFormatter {
        virtual ~IFormatter() {}
        virtual void format(std::string &buf, LogMsg *msg) = 0;
    };

    struct AsyncLogger {
        explicit AsyncLogger(size_t queue_size)
            : psink()
            , q(queue_size)
            , level(ALOG_LVL_DEBUG)
            , stopped(false)
            , flush_interval_ms(100)
        {}

        ~AsyncLogger();

        // public
        void set_sink(const ILogSink::Ptr &sink) {
            this->psink = sink;
            this->psink->logger = this;
        }
        void log(LevelType level, const char *fmt, ...);
        bool sink(LogMsg *msg);
        void flush();
        void recycle(LogMsg *msg);
        LogMsg *create();
        void set_level(LevelType level);
        bool should_log(LevelType level);
        void start();
        void stop();    // not thread safe

        // private
        void _worker_thread(ILogSink::Ptr sink);
        void _internal_log(LevelType level, const char *fmt, ...);

        static pid_t get_tid();

        // private
        ILogSink::Ptr psink;
        MPMCBoundedQueue<LogMsg *> q;
        turf::Atomic<LevelType> level;
        boost::scoped_ptr<boost::thread> sink_thread;
        bool stopped;

        struct Stats {
            turf::Atomic<uint64_t> total;
            turf::Atomic<uint64_t> drop;
            turf::Atomic<uint64_t> err;

            Stats()
                : total(0), drop(0), err(0)
            {}
        } stats;

        // params
        uint32_t flush_interval_ms;
    };

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
        LogMsg *msg = new LogMsg;
        msg->type = MSGTYPE_STOP;
        while (!this->q.try_push_back(msg)) {}
        this->sink_thread->join();
        this->stopped = true;
    }

    inline AsyncLogger::~AsyncLogger() {
        this->stop();
    }

    inline bool AsyncLogger::sink(LogMsg *msg) {
        assert(msg != NULL);
        return this->q.try_push_back(msg);
    }

    inline void AsyncLogger::flush() {
        LogMsg *msg = new LogMsg;
        msg->type = MSGTYPE_FLUSH;
        while (!this->q.try_push_back(msg)) {}
    }

    inline void AsyncLogger::recycle(LogMsg *msg) {
        assert(msg != NULL);
        delete msg;
    }

    inline LogMsg *AsyncLogger::create() {
        return new LogMsg;
    }

    inline void AsyncLogger::set_level(LevelType level) {
        this->level.store(level, turf::Relaxed);
    }

    inline bool AsyncLogger::should_log(LevelType level) {
        return level >= this->level.load(turf::Relaxed);
    }

    inline void AsyncLogger::start() {
        assert(this->psink.get() != NULL);
        assert(this->sink_thread.get() == NULL);
        this->sink_thread.reset(new boost::thread(boost::bind(&AsyncLogger::_worker_thread, this, this->psink)));
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

    inline void AsyncLogger::_worker_thread(ILogSink::Ptr sink) {
        assert(sink.get() != NULL);

        size_t attempts = 0;
        uint64_t last_flush = _get_time_msec();
        while (true) {
            LogMsg *msg = NULL;
            if (!this->q.try_pop_front(msg)) {
                // queue empty, wait a moment
                bool sleeped = _wait_a_moment(++attempts);
                if (sleeped) {
                    // check for flush
                    uint64_t now = _get_time_msec();
                    if (now >= last_flush + this->flush_interval_ms) {
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
                return;     // thread exit
            case MSGTYPE_FLUSH:
                delete msg;
                last_flush = _get_time_msec();
                sink->flush();
                break;
            case MSGTYPE_LOG:
                // check for flush before msg is deleted
                if (_timeval_to_msec(msg->time) >= last_flush + this->flush_interval_ms) {
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

    inline void AsyncLogger::_internal_log(LevelType level, const char *fmt, ...) {
        // TODO: ...
        fprintf(stderr, "[AsyncLogger::_internal_log] ");
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }

    inline void AsyncLogger::log(LevelType level, const char *fmt, ...) {
        LogMsg &msg = *this->create();
        msg.type = MSGTYPE_LOG;
        msg.level = level;
        ::gettimeofday(&msg.time, NULL);
        msg.tid = this->get_tid();

        va_list ap;
        va_start(ap, fmt);
        char buf[TZ_ASYNCLOG_MAX_LEN];
        int n = TZ_ASYNCLOG_VSNPRINTF(buf, sizeof(buf), fmt, ap);
        // int n = TZ_ASYNCLOG_VSNPRINTF(msg.msg_data, sizeof(msg.msg_data), fmt, ap);
        va_end(ap);
        if (n > 0 && n < (int)sizeof(buf)) {
            msg.msg.append(buf, (size_t)n);
            // msg.msg_size = (size_t)n;
        } else {
            // TODO: stats
            msg.msg.append("[AsyncLogger] bad vsnprintf call: ");
            msg.msg.append(fmt);
            // char *end = strncpy(msg.msg_data, "[AsyncLogger] bad vsnprintf call: ", sizeof(msg.msg_data));
            // strncat(end, fmt, (char *)msg.msg_data + sizeof(msg.msg_data) - end);
            // msg.msg_size = strlen(msg.msg_data);
        }

        if (!this->sink(&msg)) {
            this->recycle(&msg);
            this->stats.drop.fetchAdd(1, turf::Relaxed);
        }
        this->stats.total.fetchAdd(1, turf::Relaxed);
    }

#define TZ_ASYNC_LOG(logger, level, fmt, ...) \
    do { \
        if (logger.should_log(level)) { \
            logger.log(level, fmt, ##__VA_ARGS__); \
        } \
    } while (0)


}}  // ::tz::asynclog
