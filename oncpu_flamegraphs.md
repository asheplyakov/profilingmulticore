# On-CPU profiling

## Introduction: thundering herd problem

Many threads wake up on the same event, but only one can handle it.

* `producer`: enqueues IO requests to save results of the computation
* `workers`: execute IO requests (or rather simulate that)

[complete source](./src/thunderingherd.cpp)

```c++
struct Queue {
    std::queue<int> q;
    std::mutex mutex;
    std::condition_variable cond_nonempty;
    std::condition_variable cond_nonfull;
    unsigned max_size{128U};
    bool finished{false};
};
```

```c++
void worker(std::shared_ptr<Queue> qptr, unsigned serviceTimeUsec) {
    for (;;) {
        std::remove_reference<decltype(qptr->q.front())>::type item;
        {
           std::unique_lock<decltype(qptr->mutex)> l(qptr->mutex);
           if (qptr->q.empty() && !qptr->finished) {
               qptr->cond_nonempty.wait(l, [&] { return !qptr->q.empty() || qptr->finished; });
           }
           if (qptr->q.empty() && qptr->finished) {
               break;
           }
           item = qptr->q.front();
           item++;
           qptr->q.pop();
        }
        qptr->cond_nonfull.notify_one();
        if (serviceTimeUsec > 0) {
           std::this_thread::sleep_for(std::chrono::microseconds(serviceTimeUsec));
        }
    }
}
```

```c++
void producer(std::shared_ptr<Queue> qptr, uint64_t maxItems, unsigned periodUsec) {
    int item = 0;
    using unique_lock = std::unique_lock<decltype(qptr->mutex)>;
    for (uint64_t i = 0; i < maxItems; i++) {
        {
           unique_lock l(qptr->mutex);
           if (qptr->q.size() >= qptr->max_size) {
               qptr->cond_nonfull.wait(l, [&] { return qptr->q.size() < qptr->max_size; });
           }
           qptr->q.push(item);
        }
        qptr->cond_nonempty.notify_all();
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
}
```

#### Assumptions

* Workers are fast enough, but sometimes IO can experience a long delay
  * `producer` makes a request per a 10 usec
  * `worker` executes a request in 1 usec on average
* Program automatically starts a worker per a core

#### Expectations

* The `producer` is the bottleneck
* The run time of the program == run time of the `producer` thread

#### Reality

```bash
time ./bin/thunderingherd
19 worker threads
producer: message period 10 usec
workers: service time 1 usec
ETA: 10 sec 
Actual time: 27199 msec

real    0m27.201s
user    0m28.339s
sys     0m49.603s
```

* 3x slower than expected
* abnormally high CPU usage, especially in the kernel mode

---

## On-CPU profiling

* Finding top N code paths where the app spent most of its CPU cycles
* Checking if the app efficiently uses available cores

Method: sampling profiling

* Interrupt the program once in N (10^6) cycles 
* Record the call trace
* See which call traces happens most often

---

### Visualisation

Proper visualisation is the key

#### [Flame graphs](http://www.brendangregg.com/flamegraphs.html)

* x axis: the sample of call stacks ( **not** passage of time )
* y axis: the stack depth
* rectanlge: stack frame, width ~ number of occurrences
* top edge: function on CPU

Things which take a miniscule time to execute don't even show up

[thunderingherd flame graph](./img/thunderingherd.svg)

#### [Subsecond offset heatmaps](http://www.brendangregg.com/HeatMaps/subsecondoffset.html)

An improved version of a CPU load graph

* x axis: time, seconds
* y axis: subsecond offset (milliseconds)
* color: number of events

Useful for

* Tracking down concurrency problems: lock contention, thundering herd, etc
* Understanding the behavior of the app (IO bound vs CPU bound)


### On-CPU profiling in Linux

* [perf](https://perf.wiki.kernel.org/index.php/Main_Page)
* [bcc-tools/profile](https://github.com/iovisor/bcc)
* [FlameGraph](https://github.com/brendangregg/FlameGraph)
* [FlameScope](https://github.com/Netflix/flamescope)

#### Recording a trace

Record stack traces of the whole system for 10 seconds, 99 times a second

```bash
./bin/thunderingherd
sudo perf record -F 99 --call-graph=dwarf -a -- ./bin/thunderingherd -t 1
```

Convert to the text form (on the same system)

```bash
sudo perf script --header | gzip -9 > thunderingherd.stacks.gz
```

Examine with [flamescope](https://github.com/Netflix/flamescope)

---

## Summary

* [thundering herd](https://en.wikipedia.org/wiki/Thundering_herd_problem) is a sure way to make a multithreaded program slow
* `Thundering herd` happens when many threads wake up on the same event, but only one of them has an actual work to do
* Careless use of C++11 condition variables (`notify_all`) results in a thundering herd
* Same applies to
  * POSIX condition variables (`pthread_cond_broadcast`)
  * Synchronization with Windows manual-reset events
  * Windows overlapped IO with an unspecified concurrency
* On-CPU sampling profiling can be used to track down the problem
* Flame graphs make it easy to find bottlenecks
* Subsecond offset heat maps are helpful for tracking down concurrency issues

