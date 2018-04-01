#pragma once

#include "asynclog.hpp"


namespace tz { namespace asynclog {

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
