# LDS Empirical Performance & Benchmark Results

This document contains the official empirical benchmark results of the **Log Data Stream (LDS)** compression engine evaluated across **13 standard industry log datasets** spanning supercomputers, automotive telemetry, distributed databases, cloud clusters, and operating systems.

---

## 1. Executive Summary

| Evaluation Metric | Measured Result |
| :--- | :--- |
| **Total Datasets Evaluated** | 13 diverse real-world datasets |
| **Total Log Lines Processed** | **52,366,727 individual log entries** |
| **Total Raw Log Volume** | **5.16 GB (5,545,656,256 bytes)** |
| **Total Compressed Wire Volume**| **472.71 MB (495,677,009 bytes)** |
| **Mean Compression Ratio** | **11.19x smaller** |
| **Aggregate Bandwidth Saved** | **91.06% network traffic reduction** |
| **Average Streaming Speed** | **47,494 lines/second (4.80 MB/sec)** |
| **Decompression Speed** | **>110,000 lines/second (>12.0 MB/sec)** |
| **Bit-Exact Integrity** | **100.0% character-exact match (ALL PASS)** |
| **Peak RAM Budget** | **Strict ~166 KB constant ceiling (Zero leaks)** |

---

## 2. Benchmark 1: Online Single-Line Streaming Mode

In this mode, each log line is compressed individually as it is generated with **zero buffering latency** (ideal for real-time edge devices, automotive ECUs, and streaming daemons):

| Dataset Name | Domain / Origin | Lines | Raw Volume | Wire Volume | Ratio (X) | Bandwidth Saved | Lines/Sec Speed | Throughput (MB/s) | Bit-Exact Verified |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`logs.txt`** | Generic Application | 2,140 | 299.58 KB | **21.20 KB** | **14.13x** | **92.9%** | 33,744 | 4.61 MB/s | PASS (100%) |
| **`automotive_telematics.log`** | Automotive CAN / BMS | 10,000 | 1.12 MB | **160.24 KB** | **7.16x** | **86.0%** | 6,868 | 0.77 MB/s | PASS (100%) |
| **`Linux.txt`** | Linux Kernel & Daemons | 25,567 | 2.22 MB | **248.54 KB** | **9.13x** | **89.0%** | 22,991 | 1.99 MB/s | PASS (100%) |
| **`Apache.txt`** | Web Server Access | 56,482 | 4.84 MB | **246.30 KB** | **20.14x** | **95.0%** | 62,488 | 5.36 MB/s | PASS (100%) |
| **`Zookeeper.txt`** | Distributed Coordination| 74,380 | 9.80 MB | **422.18 KB** | **23.77x** | **95.8%** | 39,950 | 5.26 MB/s | PASS (100%) |
| **`HealthApp.txt`** | Mobile Telemetry | 253,394 | 21.96 MB | **2.23 MB** | **9.86x** | **89.9%** | 47,500 | 4.12 MB/s | PASS (100%) |
| **`HPC.txt`** | Supercomputer Cluster | 433,490 | 31.17 MB | **1.63 MB** | **19.08x** | **94.8%** | 65,491 | 4.71 MB/s | PASS (100%) |
| **`openstack_normal2.txt`** | OpenStack Cloud | 137,074 | 38.50 MB | **2.39 MB** | **16.09x** | **93.8%** | 45,553 | 12.79 MB/s | PASS (100%) |
| **`SSH.txt`** | SSH Auth & Security | 655,147 | 68.77 MB | **3.34 MB** | **20.61x** | **95.2%** | 55,208 | 5.79 MB/s | PASS (100%) |
| **`Android.log`** | Android System Logs | 1,555,005 | 180.40 MB | **25.47 MB** | **7.08x** | **85.9%** | 36,610 | 4.25 MB/s | PASS (100%) |
| **`BGL.txt`** | BlueGene/L Supercomputer| 4,747,963 | 704.23 MB | **32.39 MB** | **21.74x** | **95.4%** | 32,383 | 4.80 MB/s | PASS (100%) |
| **`HDFS.txt`** | Hadoop Distributed FS | 11,175,629 | 1.45 GB | **127.63 MB** | **11.62x** | **91.4%** | 44,731 | 5.94 MB/s | PASS (100%) |
| **`Spark.txt`** | Apache Spark Big Data | 33,240,456 | 2.68 GB | **276.55 MB** | **9.91x** | **89.9%** | 52,644 | 4.34 MB/s | PASS (100%) |
| **TOTALS / WEIGHTED AVERAGE** | **All 13 Datasets Combined** | **52,366,727** | **5.16 GB** | **472.71 MB** | **11.19x** | **91.06%** | **47,494** | **4.80 MB/s** | **ALL PASS** |

> ℹ️ **Throughput Clarification**:
> - **Table Speed (`47,494 lines/sec`)**: Represents **Full End-to-End Round-Trip Processing** (File I/O + Online Encoding + Wire Serialization + Full Decoding + 100% Bit-Exact String Verification).
> - **Pure Encoding Speed**: **~70,000 to >100,000 lines/sec** (~12.5 – 15.0 MB/sec).
> - **Pure Decoding Speed**: **>110,000 to >165,000 lines/sec** (~18.0 – 25.0 MB/sec).

---

## 3. Benchmark 2: Byte-Bounded Batched Block Mode (16 KB Blocks)

In this mode, log lines are collected into 16,384-byte blocks prior to transmission. This eliminates network frame headers and yields optimal compression:

| Dataset Name | Block Target | Batches | Evaluated Lines | Raw Volume | Wire Volume | Ratio (X) | Bandwidth Saved | Lines/Sec Speed | Bit-Exact Verified |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`Zookeeper.txt`** | 16 KB | 80 | 10,000 | 1.24 MB | **26.31 KB** | **48.39x** | **97.9%** | 77,950 | PASS (100%) |
| **`BGL.txt`** | 16 KB | 89 | 10,000 | 1.39 MB | **35.71 KB** | **39.96x** | **97.5%** | 147,738 | PASS (100%) |
| **`SSH.txt`** | 16 KB | 67 | 10,000 | 1.04 MB | **33.72 KB** | **31.51x** | **96.8%** | 71,418 | PASS (100%) |
| **`HPC.txt`** | 16 KB | 55 | 10,000 | 882.14 KB | **28.40 KB** | **31.07x** | **96.8%** | 179,149 | PASS (100%) |
| **`HDFS.txt`** | 16 KB | 86 | 10,000 | 1.34 MB | **57.54 KB** | **23.79x** | **95.8%** | 67,076 | PASS (100%) |
| **`Apache.txt`** | 16 KB | 61 | 10,000 | 966.66 KB | **57.95 KB** | **16.68x** | **94.0%** | 99,488 | PASS (100%) |
| **`logs.txt`** | 16 KB | 19 | 2,140 | 299.58 KB | **18.49 KB** | **16.20x** | **93.8%** | 53,775 | PASS (100%) |
| **`openstack_normal2.txt`** | 16 KB | 179 | 10,000 | 2.81 MB | **187.74 KB** | **15.35x** | **93.5%** | 57,241 | PASS (100%) |
| **`HealthApp.txt`** | 16 KB | 55 | 10,000 | 871.97 KB | **72.09 KB** | **12.10x** | **91.7%** | 74,968 | PASS (100%) |
| **`Linux.txt`** | 16 KB | 61 | 10,000 | 967.93 KB | **96.37 KB** | **10.04x** | **90.0%** | 69,422 | PASS (100%) |
| **`Spark.txt`** | 16 KB | 73 | 10,000 | 1.14 MB | **119.03 KB** | **9.79x** | **89.8%** | 50,321 | PASS (100%) |
| **`automotive_telematics.log`**| 16 KB | 72 | 10,000 | 1.12 MB | **147.56 KB** | **7.78x** | **87.1%** | 15,210 | PASS (100%) |
| **`Android.log`** | 16 KB | 69 | 10,000 | 1.08 MB | **241.96 KB** | **4.56x** | **78.1%** | 47,320 | PASS (100%) |
| **TOTALS / WEIGHTED AVERAGE** | **16 KB** | **966** | **122,140** | **15.06 MB** | **1.10 MB** | **13.73x** | **92.72%** | **55,498** | **ALL PASS** |

---

## 4. RAM Budget & Constant Memory Ceiling

Under strict profiling using Valgrind Massif and heap tracking tools, LDS enforces a **hard $O(1)$ memory ceiling**:

```
┌───────────────────────────────────────────────────────────┐
│              LDS RAM ALLOCATION BREAKDOWN                 │
├───────────────────────────────────────────────────────────┤
│ 1. Prefix Hash Table (256 buckets)           :   1.02 KB  │
│ 2. Template Storage (up to 256 active rules) :  48.00 KB  │
│ 3. Numerical Model State (Delta/Stride)      :  16.00 KB  │
│ 4. Recent String Ring Buffer (512 slots)     :  32.00 KB  │
│ 5. Per-Template LRU String Dictionaries      :  24.00 KB  │
│ 6. 2-Way Set-Associative LZ Corpus           :   8.00 KB  │
│ 7. Range Coder Dynamic Context Model         :  16.80 KB  │
│ 8. Static Scratch & Stdio Buffers            :  20.00 KB  │
├───────────────────────────────────────────────────────────┤
│ TOTAL PEAK RAM BUDGET                        : ~165.82 KB │
└───────────────────────────────────────────────────────────┘
```

> **Key Takeaway**: Unlike Gzip or Zstd whose memory scales with sliding window sizes, LDS algorithm structures never exceed **~166 KB RAM**, regardless of whether it processes 1,000 lines or 52,000,000 lines.

### 📌 Note: Desktop Process Working Set (~7 MB – 15 MB) vs. Edge Hardware RAM (~166 KB)
When benchmarking on a Windows 64-bit PC, tools report `PeakWorkingSetSize` (~7 MB to 15 MB). Here is what that number consists of:
1. **Windows 64-bit OS Baseline**: Windows maps `ntdll.dll`, `kernel32.dll`, `msvcrt.dll`, thread stacks, and CRT heap management into every 64-bit process (consuming ~5 MB to 7 MB before a single line is read).
2. **File System Disk Caching**: When streaming a massive **2.74 GB file** (`Spark.txt`) from disk, the Windows kernel maps file cache buffers into process memory.
3. **The LDS Engine Itself**: Inside that process, the `LDSEncoder` data structures consume **strictly ~165.82 KB of heap RAM**.

**On an embedded edge device / microcontroller (ARM Cortex-M4/M7, ESP32, Automotive ECU)**:
- There are no desktop OS DLLs and no multi-gigabyte disk file caches.
- Logs are passed in-memory from sensors or CAN-bus (e.g. 100-byte buffer).
- The edge device only needs **~166 KB of available RAM** to run LDS continuously with zero memory leaks.

---

## 5. Peak vs. Worst-Case Performance Breakdown

- **Highest Single-Line Compression**:
  - **Linux Dataset**: Line compressed **515.0x** (1,030 bytes reduced to 2 bytes).
  - **Spark Dataset**: Long classpath line compressed **441.0x** (882 bytes reduced to 2 bytes).
  - **Apache Dataset**: Repetitive HTTP access lines compressed **91.0x** (91 bytes reduced to 1 byte).
- **Cold-Start / Worst-Case Line**:
  - The very first line of a brand new, never-before-seen template has a short wire expansion of ~1.1x to 1.3x because it must transmit the template definition string. 
  - Starting immediately from the 2nd line onward, compression jumps to **10x – 30x**.
