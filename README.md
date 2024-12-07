# How to Run

```
mkdir BUILD-release
cd BUILD-release
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./macos-core-to-core-latency > results.log
# ./macos-core-to-core-latency -r 10 > results.log
# sudo nice -n -20 ./macos-core-to-core-latency > results.log

brew install python3
python3 -m venv path/to/venv
source path/to/venv/bin/activate
python3 -m pip install numpy pandas matplotlib

python3 ../macos-core-to-core-latency.py results.log

deactivate
```

# Tuning

The benchmark is sensitive to CPU core frequencies.
Therefore, connect your device to power avoid power throttling.
As for thermal throttling (e.g. on fanless systems like the Macbook Air), there aren't many options: External cooling, tuning for overall shorter runs.

You can change the following variables in main.cpp.
This can help in case the runs don't finish or take too long.

```
constexpr int iterationsPerExperiment  = 2000; // could be lowered to 1000
// dummyWorkloadLoopLength runs on idle threads to make sure all threads are scheduled concurrently
constexpr int dummyWorkloadLoopLength  = iterationsPerExperiment*1024; // empirical
          int targetExperiments        = 300;  // -r argument // can be lowered further
constexpr bool optionWarmup            = true;
constexpr bool optionEstimateFrequency = true;
```

`macos-core-to-core-latency.py` has the `-v` option to print out more statistics about the measurement.
Alternatively you can look at the logs directly.

# Results

## Apple M4 Pro (12C)

Cores 0, 1, 2, 3 are E-cores, the others are P-cores.

![Apple M4 Pro (12C) Core-to-Core Latency](results/241204-0-m4pro-cntvct_el0-i-2000-r-300.png?raw=true "Apple M2 Pro (12C) Core-to-Core Latency")

## Apple M2 Pro (10C)

Cores 0, 1, 2, 3 are E-cores, the others are P-cores.

![Apple M2 Pro (10C) Core-to-Core Latency](results/241204-1-m2pro-steady_clock-i-2000-r-300.png?raw=true "Apple M2 Pro (10C) Core-to-Core Latency")
