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

# Results

# Apple M4 Pro (12C)

Cores 0, 1, 2, 3 are E-cores, the others are P-cores.

# Apple M2 Pro (8C)

Cores 0, 1, 2, 3 are E-cores, the others are P-cores.
