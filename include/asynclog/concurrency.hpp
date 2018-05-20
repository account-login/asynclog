#pragma once

#include <pthread.h>
#include <stdexcept>
#include <sstream>


namespace tz { namespace asynclog {

    struct _ThreadException : public std::exception {
        _ThreadException(const std::string &msg, int err)
            : err(err)
        {
            std::stringstream ss;
            ss << msg << " [err:" << this->err << "]";
            this->msg = ss.str();
        }

        ~_ThreadException() throw() {}

        virtual const char *what() const throw() {
            return msg.c_str();
        }

        std::string msg;
        int err;
    };

    struct _Thread {
        pthread_t id;
        void *result;

        _Thread(void *func(void *arg), void *arg)
            : id(0), result(NULL)
        {
            int rc = ::pthread_create(&this->id, NULL, func, arg);
            if (rc != 0) {
                throw _ThreadException("pthread_create() failed", rc);
            }
        }

        ~_Thread() {
            (void)this->join();
        }

        void *join() {
            if (this->id != 0) {
                int rc = ::pthread_join(this->id, &this->result);
                if (rc != 0) {
                    throw _ThreadException("pthread_join() failed", rc);
                }
                this->id = 0;
            }
            return this->result;
        }
    };

}}  // ::tz::asynclog
