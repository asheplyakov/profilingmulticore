#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <vector>
#include <queue>
#include <iostream>
#include <type_traits>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <time.h>
#include <pthread.h>

struct Queue {
    std::queue<int> q;
    std::mutex mutex;
    std::condition_variable cond_nonempty;
    std::condition_variable cond_nonfull;
    unsigned max_size{128U};
    bool finished{false};
};

namespace sa {
struct steady_clock : public std::chrono::steady_clock {
    static time_point now() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return time_point(duration(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec)));
    }
};
} // namespace sa

static void spin_for(int64_t usecs) {
    auto start = sa::steady_clock::now();
    decltype(start) now;
    do {
       now = sa::steady_clock::now();
    } while(std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() < usecs);
}


void worker(std::shared_ptr<Queue> qptr, unsigned serviceTimeUsec, unsigned idx) {
    std::string name = std::string("tworker_") + std::to_string(idx);
    pthread_setname_np(pthread_self(), name.c_str());
    for (;;) {
        std::remove_reference<decltype(qptr->q.front())>::type item;
        {
           std::unique_lock<decltype(qptr->mutex)> l(qptr->mutex);
           qptr->cond_nonempty.wait(l, [&] { return !qptr->q.empty() || qptr->finished; });
           if (qptr->q.empty() && qptr->finished) {
               break;
           }
           item = qptr->q.front();
           qptr->q.pop();
        }
        qptr->cond_nonfull.notify_one();
        item++; // do something stupid
        if (serviceTimeUsec > 0) {
           // simulate some blocking IO
           std::this_thread::sleep_for(std::chrono::microseconds(serviceTimeUsec));
        }
    }
}

void producer(std::shared_ptr<Queue> qptr, uint64_t maxItems, unsigned periodUsec) {
    pthread_setname_np(pthread_self(), "tproducer");
    int item = 0;
    std::size_t outstanding_requests = 0;
    std::size_t notify_one_count = 0, notify_all_count = 0;
    using unique_lock = std::unique_lock<decltype(qptr->mutex)>;

    for (uint64_t i = 0; i < maxItems; i++) {
        {
           unique_lock l(qptr->mutex);
           qptr->cond_nonfull.wait(l, [&] { return qptr->q.size() < qptr->max_size; });
           qptr->q.push(item);
           outstanding_requests = qptr->q.size();
        }
        // Linux' `futex_wake` allows to wake up an arbitrary number of threads.
        // However with `std::condition_variable` it's either one or all
        if (outstanding_requests <= 1) {
            qptr->cond_nonempty.notify_one();
            ++notify_one_count;
        } else {
            qptr->cond_nonempty.notify_all();
            ++notify_all_count;
        }

        item++;
        if (periodUsec > 0) {
            spin_for(periodUsec);
        }
   }
   {
        unique_lock l(qptr->mutex);
        qptr->finished = true;
   }
   qptr->cond_nonempty.notify_all();
   std::cout << "producer: notify_all_count " << notify_all_count << std::endl;
   std::cout << "producer: notify_one_count " << notify_one_count << std::endl;
}

struct Conf {
    unsigned msgCount{1000000U};
    unsigned msgPeriodUsec{10U};
    unsigned workerCount{0U};
    unsigned workerServiceTimeUsec{1U};

    void parse(int argc, char **argv);
};

void run(const Conf& conf) {
    auto qptr = std::make_shared<Queue>();
    uint64_t producerEta = uint64_t(conf.msgCount)*conf.msgPeriodUsec;
    uint64_t consumerEta = (uint64_t(conf.msgCount)*conf.workerServiceTimeUsec)/conf.workerCount;
    uint64_t eta = std::max(consumerEta, producerEta);
    std::cout << conf.workerCount << " worker threads" << std::endl
              << "producer: message period " << conf.msgPeriodUsec << " usec" << std::endl
              << "workers: service time " << conf.workerServiceTimeUsec << " usec" << std::endl
              << "ETA: " << double(eta)/1000000U << " sec " << std::endl;

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (unsigned i = 0; i < conf.workerCount; i++) {
         workers.emplace_back(worker, qptr, conf.workerServiceTimeUsec, i);
    }
    std::thread prod{producer, qptr, conf.msgCount, conf.msgPeriodUsec};
    prod.join();
    for (auto& t: workers) { t.join(); }

    auto end = std::chrono::steady_clock::now();
    std::cout << "Actual time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " msec" << std::endl;
}

static void print_help() {
    std::cout << "thunderingherd [-n msg_count] [-p msg_period] [-t worker_threads] [-s service_time] [-h]" << std::endl;
}

void Conf::parse(int argc, char **argv) {
    int i = 1;
    auto nextArg = [&]() {
         if (i >= argc) {
             throw std::invalid_argument(std::string("parameter ") + argv[i] + " requires an argument");
         } else {
             i++;
             return argv[i];
         }
    };
    const std::unordered_map<std::string, std::function<void()>> optHandlers = {
        {"-n", [&] { msgCount = std::atoi(nextArg()); }},
        {"-t", [&] { workerCount = std::atoi(nextArg()); }},
        {"-p", [&] { msgPeriodUsec = std::atoi(nextArg()); }},
        {"-s", [&] { workerServiceTimeUsec = std::atoi(nextArg()); }},
        {"-h", [] { print_help(); exit(0); }},
    };
    for (; i < argc; i++) {
        auto handlerIt = optHandlers.find(argv[i]);
        if (handlerIt != optHandlers.end()) {
            (handlerIt->second)();
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            exit(1);
        }
    }
    if (workerCount <= 0) {
        workerCount = std::max(1U, std::thread::hardware_concurrency() - 1U);
    }
}

int main(int argc, char** argv) {
    Conf conf;
    conf.parse(argc, argv);
    run(conf);
    return 0;
}
