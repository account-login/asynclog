#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>
#include <syslog.h>
#include <iostream>

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/thread/thread.hpp>


using namespace std;


static uint64_t get_time_usec() {
    timespec tv = {0, 0};
    clock_gettime(CLOCK_REALTIME, &tv);
    return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}


void producer(boost::mutex *start, size_t id, size_t count) {
    start->lock();
    uint64_t producer_begin_us = get_time_usec();
    for (size_t i = 0; i < count; ++i) {
        ::syslog(LOG_INFO, "syslog message (%zu, %zu) : This is some text for your pleasure", id, i);
    }
    uint64_t producer_done_us = get_time_usec();
    uint64_t duration = producer_done_us - producer_begin_us;
    double qps = 1000000.0 * count / duration;
    ::syslog(LOG_INFO, "[id:%zu][duration_us:%lu][qps:%.02f]", id, duration, qps);
    start->unlock();
}


struct Args {
    size_t producer;
    size_t works;
    string sink;
};


Args parse_arge(int argc, char **argv) {
    Args args;
    args.producer = 1;
    args.works = 10 * 1000 * 1000;

    struct option long_options[] = {
        {"producer",    required_argument, 0, 'p'},
        {"work",        required_argument, 0, 'n'},
        {"sink",        required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    while (true) {
        /* getopt_long stores the option index here. */
        int option_index = 0;
        int c = getopt_long (argc, argv, "p:n:s:m:",
            long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 0:
            break;
        case 'n':
            args.works = (size_t)atol(optarg);
            break;
        case 'p':
            args.producer = (size_t)atol(optarg);
            break;
        case 's':
            args.sink = optarg;
            break;
        case '?':
            /* getopt_long already printed an error message. */
            break;
        default:
            abort();
        }
    }

    return args;
}


int main(int argc, char **argv) {
    openlog(NULL, LOG_PERROR, LOG_USER);    // not needed

    Args args = parse_arge(argc, argv);
    syslog(LOG_INFO, "[producer:%zu][works:%zu][sink:%s]",
        args.producer, args.works, args.sink.c_str());

    // prepare threads
    boost::mutex signals[args.producer];
    for (size_t id = 0; id < args.producer; ++id) {
        signals[id].lock();
    }

    boost::ptr_vector<boost::thread> threads;
    for (size_t id = 0; id < args.producer; ++id) {
        threads.push_back(new boost::thread(boost::bind(producer, &signals[id], id, args.works)));
    }

    sleep(1);

    // start
    syslog(LOG_INFO, "starting %zu producer", args.producer);
    uint64_t start_us = get_time_usec();
    for (size_t id = 0; id < args.producer; ++id) {
        signals[id].unlock();
    }

    // wait for producer
    for (size_t id = 0; id < args.producer; ++id) {
        threads[id].join();
        syslog(LOG_INFO, "producer %zu joined", id);
    }
    uint64_t producer_done_us = get_time_usec();

    syslog(LOG_INFO, "[producers:%zu][works:%zu][prod_us:%lu]",
        args.producer, args.works, producer_done_us - start_us);
    return 0;
}
