#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

void pin2cpu(int cpu);

void __attribute__((noinline))
counterBumpThread(std::atomic<int> *cnt, unsigned N, int cpu) {
    pin2cpu(cpu);
    for (; N-- > 0;) { cnt->fetch_add(1); }
}

struct alignas(64) AlignedAtomicInt {
    std::atomic<int> val;
};

void run(unsigned tCount, unsigned N, unsigned nproc) {
    std::vector<AlignedAtomicInt> counters(tCount);
    for (auto& x: counters) { x.val.store(0); }
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < tCount; i++) {
        threads.emplace_back(counterBumpThread, &(counters[i].val), N, (int)(i % nproc));
    }
    for (auto& t: threads) { t.join(); }
}

void pin2cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
        fprintf(stderr, "pthread_setaffinity_np failed\n");
        exit(1);
    }
}

int main(int argc, char** argv) {
    unsigned threads = 0, repetitions = 0, nproc = 1;
    int opt;
    while ((opt = getopt(argc, argv, "t:r:")) != -1) {
        switch (opt) {
            case 't':
                threads = std::atoi(optarg);
                break;
            case 'r':
                repetitions = std::atoi(optarg);
                break;
        }
    }
    nproc = (unsigned)sysconf(_SC_NPROCESSORS_ONLN);
    if (0 == threads) {
        threads = nproc;
    }
    if (0 == repetitions) {
        repetitions = 10000000U;
    }
    run(threads, repetitions, nproc);
    return 0;
}
    
