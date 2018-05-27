#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <streambuf>

#include "asynclog.hpp"
#include "sinks/file_sink.hpp"


namespace tz { namespace asynclog {

    struct AsyncLoggerConfig {
        std::string path;
        std::string pattern;
        std::string level;
        size_t queue_size;

        AsyncLoggerConfig() : queue_size(0) {}
    };

    struct _Parser {
        const char *begin;
        const char *end;
        const char *cur;
    };

    struct _ParserException {
        std::string what;
        explicit _ParserException(const std::string &what) : what(what) {}
    };

    inline void _throw_exc(_Parser &p, const char *fmt, ...) {
        char buf[1024] = {};

        va_list ap;
        va_start(ap, fmt);
        snprintf(buf, sizeof(buf), fmt, ap);    // ignore err
        va_end(ap);

        throw _ParserException(buf);
    }

    inline void _init_parser(_Parser &p, const std::string &input) {
        p.begin = p.cur = input.data();
        p.end = input.data() + input.size();
    }

    inline bool _is_space(char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }

    inline void _skip_space(_Parser &p) {
        while (p.cur < p.end && _is_space(*p.cur)) {
            ++p.cur;
        }
    }

    inline void _expect_remaining(_Parser &p, size_t size) {
        assert(p.cur <= p.end);
        if ((size_t)(p.end - p.cur) < size) {
            _throw_exc(p, "unexpected eof");
        }
    }

    inline void _expect_token(_Parser &p, char tok) {
        _skip_space(p);
        _expect_remaining(p, 1);
        if (*p.cur != tok) {
            _throw_exc(p, "unexpected token. expect '%c'", tok);
        }
        ++p.cur;
    }

    inline bool _maybe_token(_Parser &p, char tok) {
        _skip_space(p);
        _expect_remaining(p, 1);
        if (*p.cur == tok) {
            ++p.cur;
            return true;
        } else {
            return false;
        }
    }

    inline bool _maybe_char(_Parser &p, char tok) {
        _expect_remaining(p, 1);
        if (*p.cur == tok) {
            ++p.cur;
            return true;
        } else {
            return false;
        }
    }

    inline char _consume_char(_Parser &p) {
        _expect_remaining(p, 1);
        return *p.cur++;
    }

    inline void _expect_string(_Parser &p, std::string &obj) {
        _expect_token(p, '"');
        while (!_maybe_char(p, '"')) {
            char ch = _consume_char(p);
            if (ch == '\\') {
                char follower = _consume_char(p);
                switch (follower) {
                case '"': case '\\': case '/':
                    obj.push_back(follower);
                    break;
                case 'b':
                    obj.push_back('\b');
                    break;
                case 'f':
                    obj.push_back('\f');
                    break;
                case 'n':
                    obj.push_back('\n');
                    break;
                case 'r':
                    obj.push_back('\r');
                    break;
                case 't':
                    obj.push_back('\t');
                    break;
                case 'u':
                default:
                    _throw_exc(p, "unsupported escape");    // TODO: string
                }
            } else {
                // TODO: string
                obj.push_back(ch);
            }
        }
    }

    inline bool _maybe_digit(_Parser &p, uint8_t &digit) {
        _expect_remaining(p, 1);
        if ('0' <= *p.cur && *p.cur <= '9') {
            digit = (uint8_t)*p.cur - '0';
            ++p.cur;
            return true;
        }
        return false;
    }

    inline void _expect_uint64(_Parser &p, uint64_t &obj) {
        _skip_space(p);

        uint8_t digit = 0;
        if (!_maybe_digit(p, digit)) {
            _throw_exc(p, "expect digit");
        }

        obj = digit;
        while (_maybe_digit(p, digit)) {
            obj *= 10;
            obj += digit;
            // FIXME: check overflow
        }
    }

    template <class T>
    inline void _parse_obj(_Parser &p, void f(_Parser &p, const std::string &key, T &obj), T &obj)
    {
        _expect_token(p, '{');
        bool first = true;
        while (!_maybe_token(p, '}')) {
            if (!first) {
                _expect_token(p, ',');
            }
            first = false;

            std::string key;
            _expect_string(p, key);
            _expect_token(p, ':');
            f(p, key, obj);
        }
    }

    inline void _config_handler(_Parser &p, const std::string &key, AsyncLoggerConfig &obj) {
        if (key == "path") {
            _expect_string(p, obj.path);
        } else if (key == "pattern") {
            _expect_string(p, obj.pattern);
        } else if (key == "level") {
            _expect_string(p, obj.level);
        } else if (key == "queue_size") {
            _expect_uint64(p, obj.queue_size);
        } else {
            _throw_exc(p, "unexpected key: %s", key.c_str());
        }
    }

    bool load_config_string(AsyncLoggerConfig &config, const std::string &input, std::string &errmsg)
    {
        _Parser p;
        _init_parser(p, input);
        try {
            _parse_obj(p, _config_handler, config);
        } catch (_ParserException &exc) {
            errmsg = exc.what;
            return false;
        }
        return true;
    }

    bool load_config_file(AsyncLoggerConfig &config, const std::string &filename, std::string &errmsg)
    {
        std::ifstream ifs(filename.c_str());
        if (!ifs) {
            errmsg = "can not open config file: " + filename;
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return load_config_string(config, content, errmsg);
    }

    inline LevelType _level_from_string(const std::string &levelstr) {
        if (levelstr == "debug") {
            return ALOG_LVL_DEBUG;
        } else if (levelstr == "info") {
            return ALOG_LVL_INFO;
        } else if (levelstr == "notice") {
            return ALOG_LVL_NOTICE;
        } else if (levelstr == "warn") {
            return ALOG_LVL_WARN;
        } else if (levelstr == "error") {
            return ALOG_LVL_ERROR;
        } else if (levelstr == "fatal") {
            return ALOG_LVL_FATAL;
        } else {
            // unknown
            return ALOG_LVL_DEBUG;
        }
    }

    void config_logger(AsyncLogger &logger, AsyncLoggerConfig &config) {
        FileSink *filesink = new FileSink(config.path);
        ILogSink::Ptr sink(filesink);
        if (!config.pattern.empty()) {
            filesink->set_formatter(IFormatter::Ptr(new DefaultFormtter(config.pattern.c_str())));
            logger.set_sink(sink);
        }
        if (!config.level.empty()) {
            logger.set_level(_level_from_string(config.level));
        }
        if (config.queue_size != 0) {
            logger.set_queue_size(config.queue_size);
        }
    }

    bool config_logger_from_file(AsyncLogger &logger, const std::string &filename, std::string &errmsg)
    {
        AsyncLoggerConfig config;
        if (!load_config_file(config, filename, errmsg)) {
            return false;
        }
        config_logger(logger, config);
        return true;
    }

}}  // ::tz::asynclog
