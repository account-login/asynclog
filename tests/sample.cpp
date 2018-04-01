#include <stdio.h>
#include <time.h>

#include <boost/thread/thread.hpp>

#include "asynclog/asynclog.hpp"
#include "asynclog/sinks/fp_sink.hpp"
#include "asynclog/sinks/file_sink.hpp"


using namespace tz::asynclog;


static tz::asynclog::AsyncLogger logger(1024 * 1024);
static tz::asynclog::AsyncLogger file_logger(1024 * 1024);


void thread() {
    TZ_ASYNC_LOG(logger, ALOG_LVL_INFO, "thread started");
    timespec ts = {0, 500 * 1000 * 1000};   // 500ms
    ::nanosleep(&ts, NULL);
    TZ_ASYNC_LOG(logger, ALOG_LVL_INFO, "thread before exit");
}

void thread_file() {
    for (size_t i = 0; i < 10000; ++i) {
        TZ_ASYNC_LOG(file_logger, ALOG_LVL_INFO, "test log to file from thread %d", 123);
        timespec ts = {0, 5* 1000 * 1000};  // 5ms
        ::nanosleep(&ts, NULL);
    }
}


int main() {
    logger.set_sink(ILogSink::Ptr(new FpSink(stdout)));
    logger.start();
    file_logger.set_sink(ILogSink::Ptr(new FileSink("asynclog_sample.log")));
    file_logger.start();

    boost::thread t(thread);

    TZ_ASYNC_LOG(logger, ALOG_LVL_DEBUG, "test log %d", 123);
    TZ_ASYNC_LOG(logger, ALOG_LVL_DEBUG, "test log %s", "hello");
    timespec ts = {0, 1000 * 1000}; // 1ms
    int rv = ::nanosleep(&ts, NULL);
    TZ_ASYNC_LOG(logger, ALOG_LVL_DEBUG, "rv: %d", rv);

    TZ_ASYNC_LOG(file_logger, ALOG_LVL_DEBUG, "test log to file %d", 123);

    logger.set_level(ALOG_LVL_INFO);
    TZ_ASYNC_LOG(logger, ALOG_LVL_DEBUG, "should not see this");

    boost::thread t2(thread_file);

    t.join();
    t2.join();
    TZ_ASYNC_LOG(logger, ALOG_LVL_INFO, "main exit");
    return 0;
}
