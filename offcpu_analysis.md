# Off-CPU profiling

## Introduction

There are two types of performance problems:

* On-CPU: threads spend (too many) time running on CPU
* Off-CPU: thread(s) spend (too many) time while blocked on locks, IO, paging, etc

[Off-CPU analysis](http://www.brendangregg.com/offcpuanalysis.html):
measuring and studying off-CPU time along with context such as stack traces.


## Recap: thundering herd problem

Many threads wake up on the same event, but only can handle it.

### Toy example

* `producer`: enqueues IO requests to save results of the computation
   * CPU bound, makes a request per a 10 usec
   * calls `std::condition_variable::notify_all()` to wake up workers 
* `workers`: execute queued requests (or rather simulate that)
   * a `worker` executes (or rather simulates) a request in 1 usec on average


```c++
template<typename T> class BlockingQueue {
public:
    bool push(const T& item);
    bool pop(T& item);
    void finish();
    explicit BlockingQueue(unsigned capacity);
};
```

```c++
using WorkQueue = BlockingQueue<unsigned int>;

void worker(std::shared_ptr<WorkQueue> qptr, unsigned serviceTimeUsec) {
    typename WorkQueue::value_type item{};
    while (qptr->pop(item)) {
        item++;
        if (serviceTimeUsec > 0) {
           std::this_thread::sleep_for(std::chrono::microseconds(serviceTimeUsec));
        }
    }
}
```

```c++
void producer(std::shared_ptr<WorkQueue> qptr, uint64_t maxItems, unsigned periodUsec) {
    typename Queue::value_type item{0};
    for (uint64_t i = 0; i < maxItems; i++) {
        qptr->push(item);
        if (periodUsec > 0) {
            spin_for(periodUsec);
        }
    }
    qptr->finish();
}
```

```c++
tempate<typename T> bool BlockingQueue<T>::push(const T& item) {
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
```

```c++
template<typename T> bool BlockingQueue<T>::pop(T& item) {
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
```

[complete source](./src/thunderingherd.cpp)

#### Expectations

`producer` makes 10^6 IO requests and exits (for example)

* Elapsed run time of the program == run time of the ``producer` thread == 10 sec
* One worker can handle all messages in 1 second => a worker sleeps > 90% of the time
* With 10 workers: a worker sleeps > 99% of the (elapsed)
* The `producer` thread is the bottleneck

#### Reality

* The actual run time is 3x more than expected (20-core system, 19 worker threads)
* Both the elapsed time and the on-CPU time are excessive
* Root cause: all workers trying to acquire the same mutex
  (every time the producer submits a request)
* The problem can be tracked down by on-CPU profiling
* **There is a better method: off-CPU profiling**


## Why my threads are blocked?


### Wakeup stacks

Off-CPU stack shows the blocking path, but not the full reason it got blocked
(i.e. who was holding the lock). That reason is often with another thread,
the one that called a wakeup on a blocked thread (example: a web server with
a thread pool doing network IO).

#### Wakeup flame graphs

[thunderingherd wakeup stacks](./img/thunderingherd_wakestacks.svg)

### Off-CPU profiling in Linux

Method: tracing the scheduler (all blocking code paths in system calls end
up in the scheduler).

* [bcc-tools](https://github.com/iovisor/bcc)
  * offwaketime
  * offcputime
* [FlameGraph](https://github.com/brendangregg/FlameGraph)

##### Prerequisites

* The application must be compiled with frame pointers (`-fno-omit-frame-pointer`)
* The kernel must support [eBPF instrumentation](https://github.com/iovisor/bcc/blob/master/INSTALL.md#kernel-configuration)

##### Recording a trace

**Beware: on a busy system tracing the scheduler for a long time can make the system totally unresponsive**

Launch the program, and trace its offcpu time (and wakeup stacks) for 10 seconds 

```bash
./bin/thunderingherd &
pid=$!
sudo /usr/share/bcc/tools/offwaketime -f -p $pid 10 -M 999999999 10 > thunderingherd_wakestacks.txt
```

Make a flame graph

```bash
./tools/FlameGraph/flamegraph.pl --title 'Thundering herd' --colors wakeup --countname usec thunderingherd_wakestacks.txt > thunderingherd_wakestacks.svg
```

## Summary

* Often people care only about CPU cycles spent by the app
* Time spent by threads when they are **not** running on CPUs is also important
* Wakeup stacks are useful for finding **why** a thread was blocked
* Off-CPU profiling might incur substantial overhead, especially with workloads
  with a heavy context switching (200kps)
* Flame graphs are useful for off-CPU analysis

