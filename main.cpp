#include <mach/mach.h>
#include <mach/thread_act.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <limits>
#include <map>
#include <print>
#include <shared_mutex>
#include <thread>
#include <utility>
#include <vector>

uint64_t currentCore();
uint64_t currentTimer();
double   currentTimerPeriodNs();
double   estimateFrequencyGHz();
double   clockOverhead();
void     dummyWorkload(int loopLength);

#define CLOCK_NAME      "std::chrono::steady_clock"
#define CLOCK_NOW       std::chrono::steady_clock::now()
#define CLOCK_PERIOD_NS static_cast<double>(std::chrono::steady_clock::period::num) / std::chrono::steady_clock::period::den * 1000 * 1000 * 1000
//#define CLOCK_NAME      "std::chrono::high_resolution_clock"
//#define CLOCK_NOW       std::chrono::high_resolution_clock::now()
//#define CLOCK_PERIOD_NS static_cast<double>(td::chrono::high_resolution_clock::period::num) / td::chrono::high_resolution_clock::period::den * 1000 * 1000 * 1000
//#define CLOCK_NAME      "std::chrono::system_clock"
//#define CLOCK_NOW       std::chrono::system_clock::now()
//#define CLOCK_PERIOD_NS static_cast<double>(std::chrono::system_clock::period::num) / std::chrono::system_clock::period::den * 1000 * 1000 * 1000
//#define CLOCK_NAME      "CNTVCT_EL0"
//#define CLOCK_NOW       currentTimer()
//#define CLOCK_PERIOD_NS currentTimerPeriodNs()

constexpr bool optionWarmup            = true;
constexpr bool optionEstimateFrequency = true;

constexpr int warmupIterationsPerExperiment = optionWarmup ? 10 : 0;
constexpr int iterationsPerExperiment       = 2000;
constexpr int dummyWorkloadLoopLength       = iterationsPerExperiment*1024; // empirical - Try raising by 2-4x on M Ultra, because it currently only roughly covers an experiment at 100ns core-to-core latency
constexpr int divideByTwoBecausePingPong    = 2;
int targetExperiments                       = 300;
constexpr int validExperimentStreakMax      = 10;

std::atomic<std::size_t> totalCores = std::numeric_limits<std::size_t>::max();
std::atomic<bool>        allDone    = false;

struct Experiments {

    struct Experiment {
        std::size_t fromCoreIndex;
        std::size_t toCoreIndex;

        int validResults;
        int invalidResults;

        std::atomic<bool> toCoreFound;
        std::atomic<bool> fromCoreFound;

        std::atomic<double> fromCoreFrequencyBeforeGHz, fromCoreFrequencyAfterGHz;
        std::barrier<>      beforeBarrier;
        std::atomic<bool>   bounce;
        std::atomic<bool>   fromCoreDidNotChange;
        std::atomic<bool>   toCoreDidNotChange;
        std::atomic<double> fromCoreNanoSecondsPerIteration;
        std::barrier<>      afterBarrier;
        std::barrier<>      afterStreakBarrier;

        Experiment(std::size_t fromCoreIndex_, std::size_t toCoreIndex_) :
            fromCoreIndex(fromCoreIndex_),
            toCoreIndex(toCoreIndex_),
            validResults(0),
            invalidResults(0),
            toCoreFound(false),
            fromCoreFound(false),
            fromCoreFrequencyBeforeGHz(0.0),
            fromCoreFrequencyAfterGHz(0.0),
            beforeBarrier(2),
            bounce(true),
            fromCoreDidNotChange(false),
            toCoreDidNotChange(false),
            fromCoreNanoSecondsPerIteration(0),
            afterBarrier(2),
            afterStreakBarrier(2)
            {}

        void resetTemporaries() {
            fromCoreFound                   = false;
            toCoreFound                     = false;
            fromCoreFrequencyBeforeGHz      = 0.0;
            fromCoreFrequencyAfterGHz       = 0.0;
            bounce                          = true;
            fromCoreDidNotChange            = false;
            fromCoreNanoSecondsPerIteration = 0.0;
            toCoreDidNotChange              = false;
        }
    };

    std::vector<std::shared_ptr<Experiment>> v; // Note: shared_ptr is a workaround for construction of atomic, barrier, ...
    std::shared_mutex sm;

    std::barrier<> b1;
    std::barrier<> b2;
    std::barrier<> b3;
    std::barrier<> b4;


    Experiments(std::size_t totalCores_) :
        b1(totalCores_),
        b2(totalCores_),
        b3(totalCores_),
        b4(totalCores_)
    {
        std::unique_lock ul(sm);

        for (std::size_t fromCoreIndex = 0; fromCoreIndex < totalCores_; fromCoreIndex += 1) {
            for (std::size_t toCoreIndex = 0; toCoreIndex < totalCores_; toCoreIndex += 1) {
                if (fromCoreIndex == toCoreIndex) { continue; }
                v.emplace_back(std::make_shared<Experiment>(fromCoreIndex, toCoreIndex));
            }
        }

    }

    Experiment& top() {
        std::shared_lock sl(sm);
        return *v.at(0);
    }

};

struct Cores {
    std::vector<uint64_t>           i2c;
    std::map<uint64_t, std::size_t> c2i;
    std::shared_mutex               sm;

    std::pair<uint64_t, std::size_t> currentCoreAndIndex();
    uint64_t indexToCore(std::size_t index);

    std::string dump(std::string indent = "");
};

void f(std::size_t threadId, Experiments& experiments, Cores& cores) {

    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    //pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0); // testing
    std::println("# Info: t {:2} setting QoS class to User Interactive", threadId);

    uint64_t experimentsStarted    = 0;
    uint64_t experimentsNotStarted = 0;

    while (allDone.load(std::memory_order::acquire) == false) {
        experiments.b1.arrive_and_wait();

        auto [core, coreIndex] = cores.currentCoreAndIndex();
        //std::println(std::cerr, "t {:2}, coreIndex {:2}, core {:5}", threadId, coreIndex, core);

        auto& e = experiments.top();
        if (threadId == 0) {
            e.resetTemporaries();
            //std::println("# Debug: Top experiment: {} -> {}, valid {}, invalid {}", e.fromCoreIndex, e.toCoreIndex, e.validResults, e.invalidResults);
        }

        uint64_t fromCore = cores.indexToCore(e.fromCoreIndex); // Note: only needed for toCore's printout

        experiments.b2.arrive_and_wait();

        bool iAmFromCore = false;
        bool iAmToCore   = false;
        if (coreIndex == e.fromCoreIndex) {
            bool falseConstant = false;
            iAmFromCore = e.fromCoreFound.compare_exchange_strong(falseConstant, true);
             //std::println("# Debug: t {}, ci {}, iAmFromCore.", threadId, coreIndex);
        } else if (coreIndex == e.toCoreIndex) {
            bool falseConstant = false;
            iAmToCore   = e.toCoreFound.compare_exchange_strong(falseConstant, true);
            //std::println("# Debug: t {}, ci {}, iAmToCore."  , threadId, coreIndex);
        }
        bool iAmFromOrToCore = iAmFromCore || iAmToCore;

        experiments.b3.arrive_and_wait();

        if (e.fromCoreFound == false || e.toCoreFound == false) {
            if (threadId == 0) {
                experimentsNotStarted += 1;
                if (experimentsNotStarted % 1000 == 0) { std::println(std::cerr, "# Debug: Not both {} and {} found ({}). Repeat.", e.fromCoreIndex, e.toCoreIndex, experimentsNotStarted); }
            }
            dummyWorkload(256); // empirical
            continue;
        } else {
            if (threadId == 0) {
                experimentsStarted += 1;
                if (experimentsStarted % 1000 == 0) { std::println(std::cerr, "# Debug: Both {} and {} found ({}).", e.fromCoreIndex, e.toCoreIndex, experimentsStarted); }
            }
        }

        if (iAmFromOrToCore) {
            //std::println(std::cerr, "t {} ci {} -- {} {}", threadId, coreIndex, e.fromCoreFound.load(), e.toCoreFound.load());

            bool validExperiment = false;
            int validExperimentStreak = 0;
            int experimentsRemaining = targetExperiments - e.validResults;
            int validExperimentStreakMax2 = std::min(experimentsRemaining, validExperimentStreakMax);

            do {
                std::atomic<double> toCoreFrequencyBeforeGHz;
                if (iAmFromCore) {
                    e.fromCoreFrequencyBeforeGHz = estimateFrequencyGHz();
                } else if (iAmToCore) {
                    toCoreFrequencyBeforeGHz = estimateFrequencyGHz();

                }

                e.beforeBarrier.arrive_and_wait();

                if (iAmFromCore) {

                    for (int i = 0; i < warmupIterationsPerExperiment; i += 1) {
                        bool trueConstant = true;
                        while(!e.bounce.compare_exchange_strong(trueConstant, false, std::memory_order::relaxed, std::memory_order::relaxed)) {
                            trueConstant = true;
                        }
                    }

                    auto startTime = CLOCK_NOW;

                    for (int i = 0; i < iterationsPerExperiment; i += 1) {
                        bool trueConstant = true;
                        while(!e.bounce.compare_exchange_strong(trueConstant, false, std::memory_order::relaxed, std::memory_order::relaxed)) {
                            trueConstant = true;
                        }
                    }

                    auto endTime = CLOCK_NOW;

                    uint64_t coreAfter                = currentCore();
                    e.fromCoreFrequencyAfterGHz       = estimateFrequencyGHz();
                    e.fromCoreDidNotChange            = coreAfter == core;
                    auto nanoSeconds                  = std::chrono::duration<double, std::nano>(endTime - startTime).count();
                    e.fromCoreNanoSecondsPerIteration = nanoSeconds / iterationsPerExperiment / divideByTwoBecausePingPong;

                    e.afterBarrier.arrive_and_wait();
                    validExperiment = e.fromCoreDidNotChange && e.toCoreDidNotChange;
                    if (validExperiment) { validExperimentStreak += 1; }

                } else if (iAmToCore) {

                    for (int i = 0; i < warmupIterationsPerExperiment; i += 1) {
                        bool falseConstant = false;
                        while(!e.bounce.compare_exchange_strong(falseConstant, true, std::memory_order::relaxed, std::memory_order::relaxed)) {
                            falseConstant = false;
                        }
                    }

                    auto startTime = CLOCK_NOW;

                    for (int i = 0; i < iterationsPerExperiment; i += 1) {
                        bool falseConstant = false;
                        while(!e.bounce.compare_exchange_strong(falseConstant, true, std::memory_order::relaxed, std::memory_order::relaxed)) {
                            falseConstant = false;
                        }
                    }

                    auto endTime = CLOCK_NOW;

                    uint64_t coreAfter                          = currentCore();
                    std::atomic<double> toCoreFrequencyAfterGHz = estimateFrequencyGHz();
                    double fromCoreFrequencyBeforeGHz           = e.fromCoreFrequencyBeforeGHz;
                    e.toCoreDidNotChange                        = coreAfter == core;

                    e.afterBarrier.arrive_and_wait();

                    validExperiment = e.fromCoreDidNotChange && e.toCoreDidNotChange;
                    if (validExperiment) {
                        validExperimentStreak += 1;
                        auto nanoSeconds = std::chrono::duration<double, std::nano>(endTime - startTime).count();
                        auto nanoSecondsPerIteration = nanoSeconds / iterationsPerExperiment / divideByTwoBecausePingPong;
                        e.validResults += 1;
                        std::println("  {:2}, {:2},    {:5}, {:5},    {:6.2f} ns, {:6.2f},    {:.2f} GHz, {:.2f},    {:.2f}, {:.2f}",
                            e.fromCoreIndex,
                            e.toCoreIndex,
                            fromCore,
                            core,
                            e.fromCoreNanoSecondsPerIteration.load(),
                            nanoSecondsPerIteration,
                            fromCoreFrequencyBeforeGHz,
                            e.fromCoreFrequencyAfterGHz.load(),
                            toCoreFrequencyBeforeGHz.load(),
                            toCoreFrequencyAfterGHz.load());
                    } else {
                        e.invalidResults += 1;
                        //std::println("  {} -> {}: invalid", e.fromCoreIndex, e.toCoreIndex);
                    }
                }
            } while (validExperiment && validExperimentStreak < validExperimentStreakMax2);

            e.afterStreakBarrier.arrive_and_wait();

            if (iAmToCore) {
                std::stable_sort(std::begin(experiments.v), std::end(experiments.v), [](const auto& e, const auto& f) { return e->validResults < f->validResults; });
                if (experiments.v[0]->validResults >= targetExperiments) {
                    allDone = true;
                }
            }

        } else {
            dummyWorkload(dummyWorkloadLoopLength);
        }

        experiments.b4.arrive_and_wait();
    }
}

int main(int argc, char **args) {

    Cores cores;

    totalCores = (std::size_t) std::thread::hardware_concurrency();
    //totalCores = 4; // testing

    auto helpText = "macos-core-to-core-latency [-r,--runs num]";

    for (int i = 1; i < argc; i += 1) {
        if (std::string(args[i]) == "-h" || std::string(args[i]) == "--help") {
            std::println("{}", helpText);
            return 0;
        } else if (std::string(args[i]) == "-r" || std::string(args[i]) == "--runs") {
            if (argc < i+2) {
                std::println("Error: Too few arguments");
                return 0;
            }
            targetExperiments = std::stoi(args[i+1]);
            i += 1;
        }
    }

    std::println("# Info: Core count: {}"                , int(totalCores));
    std::println("# Info: Iterations per experiment: {}" , iterationsPerExperiment);
    std::println("# Info: Experiments per core pair: {}" , targetExperiments);

    std::println("# Info: Clock: {}"                     , CLOCK_NAME);
    std::println("# Info: Clock period: {:.0f}ns"        , CLOCK_PERIOD_NS);
    std::println("# Info: Clock query overhead: {:.0f}ns", clockOverhead());


    constexpr int dummyWorkloadBenchmarkIterations = 30;
    double dummyWorkloadBenchmarkNanoSeconds = std::numeric_limits<double>::max();
    for (int i = 0; i < dummyWorkloadBenchmarkIterations; ++i) {
        auto dummyWorkloadBenchmarkStartTime = CLOCK_NOW;
        dummyWorkload(dummyWorkloadLoopLength);
        auto dummyWorkloadBenchmarkEndTime = CLOCK_NOW;
        auto dummyWorkloadBenchmarkNanoSeconds_ = std::chrono::duration<double, std::nano>(dummyWorkloadBenchmarkEndTime - dummyWorkloadBenchmarkStartTime).count();
        dummyWorkloadBenchmarkNanoSeconds = std::min(dummyWorkloadBenchmarkNanoSeconds, dummyWorkloadBenchmarkNanoSeconds_);
    }
    std::println("# Info: Dummy workload duration: {:.0f}ns", dummyWorkloadBenchmarkNanoSeconds);
    std::println("# Info: Dummy workload latency equivalent: {:.0f}ns", dummyWorkloadBenchmarkNanoSeconds / iterationsPerExperiment / divideByTwoBecausePingPong);


    Experiments experiments(totalCores);

    auto startTime = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < totalCores; t += 1) {
        threads.emplace_back(std::thread(f, t, std::ref(experiments), std::ref(cores)));
    }

    for (auto && t : threads) {
        t.join();
    }

    auto endTime = std::chrono::steady_clock::now();

    auto totalTimeSeconds  = std::chrono::duration<double>(endTime - startTime).count();

    std::println("# Info: Total time: {:.2f} s", totalTimeSeconds);

    return 0;
}

// /// Cores //////////////////////////////////////////////////////////////////

std::pair<uint64_t, std::size_t> Cores::currentCoreAndIndex() {
    uint64_t core = currentCore();
    
    std::shared_lock sl(sm);
    auto knownCore = c2i.contains(core);
    sl.unlock();

    std::size_t index;

    if (knownCore) {
        index = c2i.at(core);
    } else {
        std::unique_lock ul(sm);
        if (c2i.contains(core)) { // Note: need to double check because of a possible race-condition between the shared lock and here
            index = c2i.at(core);
        } else {
            if (i2c.size() == totalCores) {
                std::println("# Warning: There are more physical cores than expected. Will only test the first {}.", totalCores.load());
            }

            index = i2c.size();
            std::println("# Info: New core: {}, index {:2}", core, index);

            i2c.push_back(core);
            c2i[core] = index;
            ul.unlock();
        }
    }

    return std::make_pair(core, index);
}

uint64_t Cores::indexToCore(std::size_t index) {
    std::shared_lock sl(sm);
    if (index < i2c.size()) {
        return i2c[index];
    } else {
        return std::numeric_limits<uint64_t>::max();
    }


}

std::string Cores::dump(std::string indent) {
    return std::format("{}# i2c {}, c2i {}", indent, i2c, c2i);
}

// /// Misc ///////////////////////////////////////////////////////////////////
#define SREG_READ(SR)               \
    __asm__ volatile(               \
        "isb \r\n "                 \
        "mrs %0, " SR " \r\n "      \
        "isb \r\n " : "=r"(value));

uint64_t currentCore() {
    uint64_t value;
    SREG_READ("TPIDR_EL0");
    return value;
}

uint64_t currentTimer() {
    uint64_t value;
    SREG_READ("CNTVCT_EL0");
    return value;
}

double currentTimerPeriodNs() {
    uint64_t value;
    SREG_READ("CNTFRQ_EL0");
    return 1.0 / double(value) * 1000 * 1000 * 1000;
}

double estimateFrequencyGHz() {
    if (optionEstimateFrequency == false) { return std::numeric_limits<double>::quiet_NaN(); }
    constexpr int ipc = 1;
    auto startTime = CLOCK_NOW;
    __asm__ volatile(
        "mov w0, #10000\n\t"
      "1:\n\t"
        "subs w0, w0, #1\n\t"
        "bne 1b\n\t"
        :
        :
        : "w0", "cc"
    );
    auto endTime = CLOCK_NOW;
    auto nanoSeconds = std::chrono::duration<double, std::nano>(endTime - startTime).count();
    return 1 / (nanoSeconds / 10000) * ipc;
}

double clockOverhead() {
    auto startTime = CLOCK_NOW;
    constexpr int loopLength = 1000;
    for (int i = 0; i < 1000; i += 1) {
        volatile auto t = CLOCK_NOW; (void) t;
        //volatile auto u = CLOCK_NOW; (void) u; // debug
    }
    auto endTime = CLOCK_NOW;
    auto nanoSeconds = std::chrono::duration<double, std::nano>(endTime - startTime).count();
    return nanoSeconds / loopLength;
}

void dummyWorkload(int loopLength) {
    volatile int x = 1;
    for (int i = 0; i < loopLength; i += 1) {
        x += 1;
    }
}