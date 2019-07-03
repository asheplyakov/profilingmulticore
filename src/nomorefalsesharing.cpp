#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <unistd.h>

void __attribute__((noinline))
counterBumpThread(std::atomic<int> *cnt, unsigned N) {
    for (; N-- > 0;) { cnt->fetch_add(1); }
}

struct alignas(64) AlignedAtomicInt {
    std::atomic<int> val;
};

void run(unsigned tCount, unsigned N) {
    std::vector<AlignedAtomicInt> counters(tCount);
    for (auto& x: counters) { x.val.store(0); }
    std::vector<std::thread> threads;
    for (auto& x: counters) {
        threads.emplace_back(counterBumpThread, &(x.val), N);
    }
    for (auto& t: threads) { t.join(); }
}

int main(int argc, char** argv) {
    unsigned threads = 0, repetitions = 0;
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
    if (0 == threads) {
        threads = (unsigned)sysconf(_SC_NPROCESSORS_ONLN);
    }
    if (0 == repetitions) {
        repetitions = 10000000U;
    }
    run(threads, repetitions);
    return 0;
}
    
