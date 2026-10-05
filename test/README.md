# LDS Performance & Speed Benchmark Suite

This directory contains the automated benchmark testing suite for measuring the **separate Encoding Speed and Decoding Speed**, along with **compression ratio, bandwidth savings, memory ceiling, and bit-exact data integrity verification**.

---

## 📁 Directory Layout

```
test/
├── bin/
│   ├── lds_test_single.exe       # C benchmark engine with separate high-res encode/decode timers
│   └── lds_test_batch.exe        # C batch benchmark engine with separate encode/decode timers
├── src/
│   ├── lds_test_single.c         # Single-line streaming benchmark source code
│   └── lds_test_batch.c          # Batch-mode benchmark source code
├── results/                      # Automatically generated benchmark report files
│   ├── benchmark_results_single.txt
│   └── benchmark_results_batch.txt
├── run_single_test.js            # Automated runner for Single-Line Streaming mode
├── run_batch_test.js             # Automated runner for Batched Block mode
├── run_test.bat                  # 1-Click interactive Windows runner
└── Makefile                      # Build system for C benchmark engines
```

---

## ⚡ Quick Start Commands

### 1. Interactive Runner (Windows)
Double-click or run:
```cmd
run_test.bat
```
Choose from the interactive menu:
- `1`: Run **BOTH** (Single + Batch) on **FULL Datasets** (All 52.3M lines, complete benchmark)
- `2`: Run **BOTH** (Single + Batch) on **Fast Sample** (50,000 lines each, fast ~30s)
- `3`: Run Single-Line Streaming Only (FULL Datasets)
- `4`: Run Single-Line Streaming Only (Fast Sample)
- `5`: Run Batched Block Streaming Only (FULL Datasets - 16 KB)
- `6`: Run Batched Block Streaming Only (Fast Sample - 16 KB)
- `7`: Quick Test on local sample file (`data/logs.txt`)
- `8`: Rebuild C benchmark engines

---

### 2. Run Single-Line Streaming Benchmark

#### Test all datasets:
```bash
node run_single_test.js
```

#### Test all datasets with full lines (no line limit):
```bash
node run_single_test.js --full
```

#### Test a specific log file:
```bash
node run_single_test.js --file ../data/logs.txt
```

---

### 3. Run Batched Block Benchmark

#### Test all datasets with default 16 KB batch size:
```bash
node run_batch_test.js
```

#### Test with custom batch size (e.g., 32 KB blocks):
```bash
node run_batch_test.js --batch-size 32768
```

#### Test a specific log file in batch mode:
```bash
node run_batch_test.js --file ../data/logs.txt
```

---

### 4. Direct C CLI Engines (Zero Node.js Dependency)

You can also run the precompiled C benchmark binaries directly from the command line:

```bash
# Single-line test:
./bin/lds_test_single.exe "../data/logs.txt"

# Batch-mode test (with 16,384 bytes batch target):
./bin/lds_test_batch.exe "../data/logs.txt" 16384
```

---

## 📄 Output Reports

The benchmark suite writes complete, detailed reports to `results/`:
- **`results/benchmark_results_single.txt`**: Contains per-dataset statistics, highest/lowest compression samples, **Pure Encode Speed**, **Pure Decode Speed**, **Round-Trip Speed**, and the Master Comparison Table.
- **`results/benchmark_results_batch.txt`**: Contains block statistics, batch efficiency, and timing breakdowns.
