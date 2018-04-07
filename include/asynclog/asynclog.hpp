#pragma once

#include <stdint.h>
#include <string.h>
#include <time.h>       // for nanosleep
#include <sched.h>      // for sched_yield
#include <stdio.h>      // for snprintf, stderr
#include <stdarg.h>
#include <sys/types.h>  // for pid_t and etc
#include <sys/time.h>   // for gettimeofday
#if  __linux__
#   include <sys/syscall.h>
#endif
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
        size_t          msg_size;
        char            msg_data[0];
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

    static const size_t _msgpool_bins = 8;

    struct _MsgPool {
        MPMCBoundedQueue<LogMsg *> pool[_msgpool_bins];

        struct Stats {
            turf::Atomic<uint32_t> get_hit;
            turf::Atomic<uint32_t> get_miss;
            turf::Atomic<uint32_t> put_hit;
            turf::Atomic<uint32_t> put_miss;

            Stats()
                : get_hit(0), get_miss(0), put_hit(0), put_miss(0)
            {}
        } stats;

        explicit _MsgPool(size_t size) {
            for (size_t i = 0; i < _msgpool_bins; ++i) {
                this->pool[i].init(size);
            }
        }

        ~_MsgPool() {
            for (size_t i = 0; i < _msgpool_bins; ++i) {
                LogMsg *msg = NULL;
                while (this->pool[i].try_pop_front(msg)) {
                    ::free(msg);
                }
            }
        }

        LogMsg *get(size_t size) {
            uint32_t index = get_index(size);
            if (index < _msgpool_bins) {
                LogMsg *msg = NULL;
                if (this->pool[index].try_pop_front(msg)) {
                    this->stats.get_hit.fetchAdd(1, turf::Relaxed);
                    return msg;
                } else {
                    size_t extra_size = 1u << (index + 4);
                    assert(extra_size >= size);
                    this->stats.get_miss.fetchAdd(1, turf::Relaxed);
                    return (LogMsg *)::malloc(sizeof(LogMsg) + extra_size);
                }
            } else {
                this->stats.get_miss.fetchAdd(1, turf::Relaxed);
                return (LogMsg *)::malloc(sizeof(LogMsg) + size);
            }
        }

        void put(LogMsg *msg) {
            uint32_t index = get_index(msg->msg_size);
            if (index < _msgpool_bins) {
                if (this->pool[index].try_push_back(msg)) {
                    this->stats.put_hit.fetchAdd(1, turf::Relaxed);
                    return;
                }
            }

            this->stats.put_miss.fetchAdd(1, turf::Relaxed);
            ::free(msg);
        }

        // TODO: clear pool when idle

        static uint32_t get_index(size_t size) {
            if (size <= 256) {  // likely
                if (size <= 128) {
                    if (size <= 16) {
                        return 0;
                    } else if (size <= 32) {
                        return 1;
                    } else if (size <= 64) {
                        return 2;
                    } else {
                        return 3;
                    }
                } else {        // likely
                    return 4;
                }
            } else if (size <= 512) {
                return 5;
            } else if (size <= 1024) {
                return 6;
            } else if (size <= 2048) {
                return 7;
            } else {
                return 8;
            }
        }
    };

    struct AsyncLogger {
        explicit AsyncLogger(size_t queue_size)
            : psink()
            , q(queue_size)
            , msgpool(1024)
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
        LogMsg *create(size_t msg_size);
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
        _MsgPool msgpool;
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
        LogMsg *msg = new LogMsg();     // special msg type do not go through pool
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
        LogMsg *msg = new LogMsg();
        msg->type = MSGTYPE_FLUSH;
        while (!this->q.try_push_back(msg)) {}
    }

    inline void AsyncLogger::recycle(LogMsg *msg) {
        assert(msg != NULL);
        this->msgpool.put(msg);
//        ::free(msg);
    }

    inline LogMsg *AsyncLogger::create(size_t msg_size) {
        return this->msgpool.get(msg_size);
//        return (LogMsg *)::malloc(sizeof(LogMsg) + msg_size);
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
        LogMsg *msg = NULL;

        va_list ap;
        va_start(ap, fmt);
        char buf[TZ_ASYNCLOG_MAX_LEN];
        int n = TZ_ASYNCLOG_VSNPRINTF(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n > 0 && n < (int)sizeof(buf)) {
            msg = this->create((size_t)n);
            msg->type = MSGTYPE_LOG;
            msg->level = level;
            ::gettimeofday(&msg->time, NULL);
            msg->tid = this->get_tid();

            ::memcpy(msg->msg_data, buf, (size_t)n);
            msg->msg_size = (size_t)n;
        } else {
            // TODO: stats
            // TODO: clean up this
            const char *errmsg = "[AsyncLogger] bad vsnprintf call: ";

            msg = this->create(strlen(errmsg));
            msg->type = MSGTYPE_LOG;
            msg->level = level;
            ::gettimeofday(&msg->time, NULL);
            msg->tid = this->get_tid();

            ::memcpy(msg->msg_data, errmsg, strlen(errmsg));
            msg->msg_size = strlen(errmsg);
        }

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
    } while (0)


}}  // ::tz::asynclog
