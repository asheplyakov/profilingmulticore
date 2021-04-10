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

template<typename T> class BlockingQueue {
    std::queue<T> q;
    std::mutex mutex;
    std::condition_variable cond_nonempty;
    std::condition_variable cond_nonfull;
    bool finished{false};
public:
    const unsigned max_size{128U};
    using value_type = T;

    /**
     * Appends `item` to the queue.
     * If the queue is full the caller is blocked until something is
     * removed or the queue enters the `finished` state.
     * Returns `false` if the queue is in the `finished` state (the element
     * is *not* in the queue in this case), `true` otherwise (the element
     * has been added to the queue)
     */
    bool push(const T& item) {
      bool ret = false;
      while (!ret) {
	 std::unique_lock<decltype(this->mutex)> l{this->mutex};
	 this->cond_nonfull.wait(l, [&] { return this->q.size() < this->max_size || this->finished; });
	 if (this->finished) {
	    break;
	 } else {
            this->q.push(item);
	    ret = true;
	 }
      }
      this->cond_nonempty.notify_all();
      return ret;
    }

    /**
     * Moves the 1st element to `item`.
     * If the queue is empty the caller is blocked until something is added
     * or the queue enters the `finished` state.
     * Returns `false` if the queue is in the `finished` state *and* is empty
     * (`item` is not changed in this case), `true` otherwise.
     */
    bool pop(T& item) {
       bool ret = false;
       while (!ret) {
          std::unique_lock<decltype(this->mutex)> l{this->mutex};
          this->cond_nonempty.wait(l, [&] { return !this->q.empty() || this->finished; });
          if (!this->q.empty()) {
	     item = this->q.front();
	     this->q.pop();
	     ret = true;
	  } else if (this->finished) {
	     break;
	  }
       }
       this->cond_nonfull.notify_one();
       return ret;
    }

    void finish() {
      {
         std::unique_lock<decltype(this->mutex)> l{this->mutex};
	 this->finished = true;
      }
      this->cond_nonempty.notify_all();
    }

    BlockingQueue() = default;
    explicit BlockingQueue(unsigned size) : max_size{size} { }
};

using Queue = BlockingQueue<unsigned int>;

static void spin_for(int64_t usecs) {
    auto start = std::chrono::steady_clock::now();
    decltype(start) now;
    do {
       now = std::chrono::steady_clock::now();
    } while(std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() < usecs);
}


void worker(std::shared_ptr<Queue> qptr, unsigned serviceTimeUsec, unsigned idx) {
    std::string name = std::string("tworker_") + std::to_string(idx);
    pthread_setname_np(pthread_self(), name.c_str());
    typename Queue::value_type item{};
    while (qptr->pop(item)) {
	item++;
	if (serviceTimeUsec > 0) {
           // simulate some blocking IO
           std::this_thread::sleep_for(std::chrono::microseconds(serviceTimeUsec));
	}
    }
}

void producer(std::shared_ptr<Queue> qptr, uint64_t maxItems, unsigned periodUsec) {
    pthread_setname_np(pthread_self(), "tproducer");
    unsigned int item = 0;

    for (uint64_t i = 0; i < maxItems; i++) {
	qptr->push(item);
        item++;
        if (periodUsec > 0) {
	    // simulate CPU-bound calculation
            spin_for(periodUsec);
        }
    }
    qptr->finish();
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
        workerCount = std::max(1U, std::thread::hardware_concurrency());
    }
}

int main(int argc, char** argv) {
    Conf conf;
    conf.parse(argc, argv);
    run(conf);
    return 0;
}
