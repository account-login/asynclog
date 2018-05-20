#include <cassert>

#include <turf/Atomic.h>


namespace tz { namespace asynclog {

    // http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
    template <typename T>
    class mpmc_bounded_queue
    {
    public:
        explicit mpmc_bounded_queue(size_t buffer_size)
            : buffer_(NULL)
            , buffer_mask_(0)
        {
            this->reset(buffer_size);
        }

        mpmc_bounded_queue() : buffer_(NULL), buffer_mask_(0) {}

        void reset(size_t buffer_size) {
            assert((buffer_size >= 2) && ((buffer_size & (buffer_size - 1)) == 0));

            if (buffer_) {
                delete[] buffer_;
            }

            buffer_ = new cell_t[buffer_size];
            buffer_mask_ = buffer_size - 1;
            for (size_t i = 0; i < buffer_size; ++i) {
                buffer_[i].sequence_.store(i, turf::Relaxed);
            }
            enqueue_pos_.store(0, turf::Relaxed);
            dequeue_pos_.store(0, turf::Relaxed);
        }

        ~mpmc_bounded_queue()
        {
            delete []buffer_;
        }

        bool enqueue(T const& data)
        {
            cell_t* cell;
            size_t pos = enqueue_pos_.load(turf::Relaxed);
            for (;;)
            {
                cell = &buffer_[pos & buffer_mask_];
                size_t seq = cell->sequence_.load(turf::Acquire);
                intptr_t dif = (intptr_t)seq - (intptr_t)pos;
                if (dif == 0)
                {
                    if (enqueue_pos_.compareExchangeWeak(pos, pos + 1, turf::Relaxed, turf::Relaxed))
                        break;
                }
                else if (dif < 0)
                    return false;
                else
                    pos = enqueue_pos_.load(turf::Relaxed);
            }

            cell->data_ = data;
            cell->sequence_.store(pos + 1, turf::Release);

            return true;
        }

        bool dequeue(T& data)
        {
            cell_t* cell;
            size_t pos = dequeue_pos_.load(turf::Relaxed);
            for (;;)
            {
                cell = &buffer_[pos & buffer_mask_];
                size_t seq = cell->sequence_.load(turf::Acquire);
                intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
                if (dif == 0)
                {
                    if (dequeue_pos_.compareExchangeWeak(pos, pos + 1, turf::Relaxed, turf::Relaxed))
                        break;
                }
                else if (dif < 0)
                    return false;
                else
                    pos = dequeue_pos_.load(turf::Relaxed);
            }

            data = cell->data_;
            cell->sequence_.store(pos + buffer_mask_ + 1, turf::Release);

            return true;
        }

    private:
        struct cell_t
        {
            turf::Atomic<size_t>    sequence_;
            T                       data_;
        };

        typedef char                cacheline_pad_t[64];

        cacheline_pad_t             pad0_;
        cell_t*                     buffer_;
        size_t                      buffer_mask_;
        cacheline_pad_t             pad1_;
        turf::Atomic<size_t>        enqueue_pos_;
        cacheline_pad_t             pad2_;
        turf::Atomic<size_t>        dequeue_pos_;
        cacheline_pad_t             pad3_;

        mpmc_bounded_queue(mpmc_bounded_queue const&);
        void operator = (mpmc_bounded_queue const&);
    };

    template <class T>
    struct MPMCBoundedQueue {
        explicit MPMCBoundedQueue(size_t size) : q(size) {}

        MPMCBoundedQueue() : q() {}

        void reset(size_t size) {
            this->q.reset(size);
        }

        bool try_push_back(const T &obj) {
            return this->q.enqueue(obj);
        }

        bool try_pop_front(T &obj) {
            return this->q.dequeue(obj);
        }

        mpmc_bounded_queue<T> q;
    };

}}  // ::tz::asynclog
