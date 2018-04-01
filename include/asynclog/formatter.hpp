#pragma once

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <libgen.h>     // for basename
#include <vector>
#include <map>

#include "asynclog.hpp"
#include "helper.hpp"


namespace tz { namespace asynclog {

#define TZ_ASYNCLOG_DEFAULT_PATTERN "%(yyyy-mm-dd) %(hh:mm:ss).%(msec) %(level) %(process)[%(tid)] %(msg)"

    struct DefaultFormtter;

    typedef void (*_SpecFunc)(DefaultFormtter &fmt, std::string &buf, LogMsg *msg);

    struct _Spec {
        _SpecFunc func;
        std::string data;

        _Spec(_SpecFunc func, const char *input)
            : func(func), data(input)
        {}

        _Spec(_SpecFunc func, std::string &input)
            : func(func)
        {
            this->data.swap(input);
        }
    };

    inline _SpecFunc _name_to_func(const std::string &name);
    inline size_t _sum_specs_size(const std::vector<_Spec> &specs);

    inline void _parse_pattern(const char *pattern, std::vector<_Spec> &specs) {
        specs.clear();

        enum { S_S, S_P, S_L } state = S_S;
        std::string data;

        size_t len = ::strlen(pattern);
        for (size_t i = 0; i <= len; ++i) {
            char ch = pattern[i];
            switch (state) {
            case S_S: {
                if (ch == '\0') {
                    if (!data.empty()) {
                        specs.push_back(_Spec(NULL, data));
                    }
                    return;
                } else if (ch == '%') {
                    if (!data.empty()) {
                        specs.push_back(_Spec(NULL, data));
                    }
                    state = S_P;
                } else {
                    data.push_back(ch);
                }
            } break;
            case S_P: {
                if (ch == '%') {
                    data.push_back('%');
                    state = S_S;
                } else if (ch == '(') {
                    state = S_L;
                } else if (ch == '\0') {
                    // bad
                    specs.push_back(_Spec(NULL, "%"));
                    return;
                } else {
                    // bad pattern, treat this as plain string
                    data.push_back('%');
                    data.push_back(ch);
                    state = S_S;
                }
            } break;
            case S_L: {
                if (ch == '\0') {
                    // bad
                    specs.push_back(_Spec(NULL, "%("));
                    return;
                } else if (ch == ')') {
                    _SpecFunc func = _name_to_func(data);
                    if (func == NULL) {
                        // bad
                        data = "%(" + data + ")";
                    } else {
                        specs.push_back(_Spec(func, ""));
                        data.clear();
                    }
                    state = S_S;
                } else {
                    data.push_back(ch);
                }
            } break;
            default: assert(!"Unreachable");
            }   // switch
        }   // for
    }

    struct _TimeCache {
        struct timeval  timeval;
        std::string     year;
        std::string     month;
        std::string     day;
        std::string     hour;
        std::string     minute;
        std::string     second;
        std::string     msec;
        std::string     yyyy_mm_dd;
        std::string     hh_mm_ss;

        _TimeCache() {
            this->timeval.tv_sec = this->timeval.tv_usec = 0;
        }

        void update(const struct timeval &tv) {
            this->timeval = tv;

            time_t ts = tv.tv_sec;
            struct tm stm;
            localtime_r(&ts, &stm);

            char buf[32];
            snprintf(buf, sizeof(buf), "%04d", stm.tm_year + 1900);
            this->year = buf;

            snprintf(buf, sizeof(buf), "%02d", stm.tm_mon + 1);
            this->month = buf;

            snprintf(buf, sizeof(buf), "%02d", stm.tm_mday);
            this->day = buf;

            this->yyyy_mm_dd = this->year + "-" + this->month + "-" + this->day;

            snprintf(buf, sizeof(buf), "%02d", stm.tm_hour);
            this->hour = buf;

            snprintf(buf, sizeof(buf), "%02d", stm.tm_min);
            this->minute = buf;

            snprintf(buf, sizeof(buf), "%02d", stm.tm_sec);
            this->second = buf;

            this->hh_mm_ss = this->hour + ":" + this->minute + ":" + this->second;

            snprintf(buf, sizeof(buf), "%03ld", tv.tv_usec / 1000);
            this->msec = buf;
        }

#define _TIMECACHE_GETTER(name) \
        const std::string &get_ ## name(const struct timeval &tv) { \
            if (this->timeval.tv_sec != tv.tv_sec) { \
                this->update(tv); \
            } \
            return this->name; \
        }

        _TIMECACHE_GETTER(year)
        _TIMECACHE_GETTER(month)
        _TIMECACHE_GETTER(day)
        _TIMECACHE_GETTER(hour)
        _TIMECACHE_GETTER(minute)
        _TIMECACHE_GETTER(second)
        _TIMECACHE_GETTER(yyyy_mm_dd)
        _TIMECACHE_GETTER(hh_mm_ss)

#undef _TIMECACHE_GETTER

        const std::string &get_msec(const struct timeval &tv) {
            if (this->timeval.tv_sec != tv.tv_sec) {
                this->update(tv);
            } else if (this->timeval.tv_usec / 1000 != tv.tv_usec / 1000) {
                this->timeval.tv_usec = tv.tv_usec;
                char buf[32];
                snprintf(buf, sizeof(buf), "%03ld", tv.tv_usec / 1000);
                this->msec = buf;
            }
            return this->msec;
        }
    };

    static const size_t _tid_cache_size = 128;

    struct _TidCache {
        struct Entry {
            uint32_t tid;
            char     digits[12];
        };

        Entry entries[_tid_cache_size];

        _TidCache() {
            ::memset(this, 0, sizeof(*this));
        }

        static size_t _hash(pid_t tid) {
            return tid;
        }

        const char *get(const LogMsg *msg) {
            assert(sizeof(Entry) == 16);
            Entry *e = &this->entries[_hash(msg->tid) % _tid_cache_size - 1];
            if (e[0].tid != 0) {
                if (e[0].tid == (uint32_t)msg->tid) {
                    return e[0].digits;
                }
                ++e;
                if (e[0].tid != 0) {
                    if (e[0].tid == (uint32_t)msg->tid) {
                        return e[0].digits;
                    }
                    if (_hash(msg->time.tv_usec) % 2) {
                        --e;
                    }
                }
            }

            // miss
            e->tid = msg->tid;
            TZ_ASYNCLOG_SNPRINTF(e->digits, sizeof(e->digits), "%u", msg->tid);
            return e->digits;
        }
    };

    struct DefaultFormtter : IFormatter {
        explicit DefaultFormtter(const char *pattern) {
            this->_init(pattern);
        }
        DefaultFormtter() {
            this->_init(TZ_ASYNCLOG_DEFAULT_PATTERN);
        }

        void _init(const char *pattern) {
            this->set_pattern(pattern);

            // default level map
            this->unknown_level = "!UNKNOWN_LEVEL!";
            std::map<LevelType, std::string> lm;
            lm[ALOG_LVL_DEBUG]  = "DEBUG ";
            lm[ALOG_LVL_INFO]   = "INFO  ";
            lm[ALOG_LVL_NOTICE] = "NOTICE";
            lm[ALOG_LVL_WARN]   = "WARN  ";
            lm[ALOG_LVL_ERROR]  = "ERROR ";
            lm[ALOG_LVL_FATAL]  = "FATAL ";
            this->set_level_map(lm);
        }

        void set_pattern(const char *pattern) {
            _parse_pattern(pattern, this->specs);
            this->specs_size = _sum_specs_size(this->specs);
        }

        virtual void format(std::string &buf, LogMsg *msg) {
            // estimate buffer size
            buf.reserve(this->specs_size + msg->msg.size() + 1);
            for (size_t i = 0; i < this->specs.size(); ++i) {
                if (this->specs[i].func == NULL) {
                    buf.append(this->specs[i].data);
                } else {
                    this->specs[i].func(*this, buf, msg);
                }
            }
        }

        void set_level_map(const std::map<LevelType, std::string> &mapping) {
            this->level_map.clear();
            std::map<LevelType, std::string>::const_iterator it = mapping.begin();
            for (; it != mapping.end(); ++it) {
                LevelType level = it->first;
                if (this->level_map.size() < (size_t)level + 1) {
                    this->level_map.resize((size_t)level + 1, this->unknown_level);
                    this->level_map[(size_t)level] = it->second;
                }
            }
        }

        const std::string &_get_level(LevelType level) const {
            if (this->level_map.size() < (size_t)level + 1) {
                return this->unknown_level;
            } else {
                return this->level_map[(size_t)level];
            }
        }

        std::vector<_Spec>          specs;
        size_t                      specs_size;
        _TimeCache                  _timecache;
        _TidCache                   _tidcache;
        std::vector<std::string>    level_map;
        std::string                 unknown_level;
    };

    // patterns
    // %%
    // %(year) %(month) %(day) %(hour) %(minute) %(second) %(msec) $(usec)
    // %(YYYY-MM-DD) %(HH:MM:SS)
    // %(level) %(msg) %(process) %(tid)

#define _SPEC_TIME(name) \
    inline void _spec_ ## name(DefaultFormtter &fmt, std::string &buf, LogMsg *msg) { \
        buf.append(fmt._timecache.get_ ## name(msg->time)); \
    }

    _SPEC_TIME(year)
    _SPEC_TIME(month)
    _SPEC_TIME(day)
    _SPEC_TIME(hour)
    _SPEC_TIME(minute)
    _SPEC_TIME(second)
    _SPEC_TIME(msec)
    _SPEC_TIME(yyyy_mm_dd)
    _SPEC_TIME(hh_mm_ss)

    inline void _spec_usec(DefaultFormtter &fmt, std::string &buf, LogMsg *msg);
    inline void _spec_level(DefaultFormtter &fmt, std::string &buf, LogMsg *msg);
    inline void _spec_msg(DefaultFormtter &fmt, std::string &buf, LogMsg *msg);
    inline void _spec_process(DefaultFormtter &fmt, std::string &buf, LogMsg *msg);
    inline void _spec_tid(DefaultFormtter &fmt, std::string &buf, LogMsg *msg);

    inline _SpecFunc _name_to_func(const std::string &name) {
#define _N2F_BRANCH(val) else if (name == #val) { return _spec_ ## val; }
        if (false) {}
        _N2F_BRANCH(year)
        _N2F_BRANCH(month)
        _N2F_BRANCH(day)
        _N2F_BRANCH(hour)
        _N2F_BRANCH(minute)
        _N2F_BRANCH(second)
        _N2F_BRANCH(msec)
        _N2F_BRANCH(usec)
        _N2F_BRANCH(level)
        _N2F_BRANCH(msg)
        _N2F_BRANCH(process)
        _N2F_BRANCH(tid)
        else if (::strcasecmp(name.c_str(), "yyyy-mm-dd") == 0) {
            return _spec_yyyy_mm_dd;
        }
        else if (::strcasecmp(name.c_str(), "hh:mm:ss") == 0) {
            return _spec_hh_mm_ss;
        } else {
            return NULL;
        }
#undef _N2F_BRANCH
    }

    inline size_t _sum_specs_size(const std::vector<_Spec> &specs) {
        size_t sum = 0;
        for (size_t i = 0; i < specs.size(); ++i) {
#define _SSS_BRANCH(val, sz) else if (specs[i].func == _spec_ ## val) { sum += (sz); }
            if (specs[i].func == NULL) {
                sum += specs[i].data.size();
            }
            _SSS_BRANCH(year, 4)
            _SSS_BRANCH(month, 2)
            _SSS_BRANCH(day, 2)
            _SSS_BRANCH(hour, 2)
            _SSS_BRANCH(minute, 2)
            _SSS_BRANCH(second, 2)
            _SSS_BRANCH(msec, 3)
            _SSS_BRANCH(usec, 6)
            _SSS_BRANCH(level, 6)
            _SSS_BRANCH(process, _get_process_name().size())
            _SSS_BRANCH(tid, 10)
            _SSS_BRANCH(yyyy_mm_dd, 10)
            _SSS_BRANCH(hh_mm_ss, 8)
            // else dont care
#undef _SSS_BRANCH
        }
        return sum;
    }

    inline void _spec_usec(DefaultFormtter &, std::string &buf, LogMsg *msg) {
        char fmtbuf[32];
        TZ_ASYNCLOG_SNPRINTF(fmtbuf, sizeof(fmtbuf), "%06ld", msg->time.tv_usec);
        buf.append(fmtbuf);
    }

    inline void _spec_level(DefaultFormtter &fmt, std::string &buf, LogMsg *msg) {
        buf.append(fmt._get_level(msg->level));
    }

    inline void _spec_msg(DefaultFormtter &, std::string &buf, LogMsg *msg) {
        buf.append(msg->msg);
        // buf.append(msg->msg_data, msg->msg_size);
    }

    inline void _spec_process(DefaultFormtter &, std::string &buf, LogMsg *) {
        buf.append(_get_process_name());
    }

    inline void _spec_tid(DefaultFormtter &fmt, std::string &buf, LogMsg *msg) {
        buf.append(fmt._tidcache.get(msg));
        // char fmtbuf[32];
        // TZ_ASYNCLOG_SNPRINTF(fmtbuf, sizeof(fmtbuf), "%d", msg->tid);
        // buf.append(fmtbuf);
    }

}}  // ::tz::asynclog
