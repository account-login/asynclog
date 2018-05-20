#pragma once

#include <unistd.h>
#include <libgen.h>

#if __cplusplus < 201103L
#   include <tr1/memory>
#else
#   include <memory>
#endif

#include "asynclog.hpp"


namespace tz { namespace asynclog {

#if __cplusplus < 201103L
#   define TZ_ASYNCLOG_SHARED_PTR std::tr1::shared_ptr
#else
#   define TZ_ASYNCLOG_SHARED_PTR std::shared_ptr
#endif

    template <class T>
    struct _StaticSingleton {
        static T instance;
    };

    template <class T>
    T _StaticSingleton<T>::instance;

    // linux only
    inline std::string _get_process_name_impl() {
        const size_t bufsize = 1024 * 4;
        char buf[bufsize] = {};
        ssize_t len = ::readlink("/proc/self/exe", buf, bufsize);
        if (len < 0) {
            return "__CAN_NOT_READ_PROC_SELF_EXE__";
        }
        return ::basename(buf);
    }

    struct _ProcessNameHolder {
        std::string value;
        _ProcessNameHolder() : value(_get_process_name_impl()) {}
    };

    inline const std::string &_get_process_name() {
        return _StaticSingleton<_ProcessNameHolder>::instance.value;
    }

}}  // ::tz::asynclog
