# LDS Algorithm Architecture & Step-by-Step Execution Flow

This document provides a comprehensive technical explanation of the **Log Data Stream (LDS)** compression engine, its core components, mathematical models, and a concrete walkthrough of both **Single-Line Streaming** and **Batched Block** encoding/decoding.

---

## 1. High-Level Core Philosophy

Standard general-purpose compressors (such as Gzip, Deflate, and Zstandard) treat data as arbitrary raw byte streams. They search for repeated substrings using sliding dictionary windows (typically 32 KB to 1 MB+ in RAM). 

While effective for static files, this approach has fundamental drawbacks when applied to real-time system logs:
1. **High Memory Overhead**: Sliding windows and hash chains require several megabytes of RAM—prohibitive for automotive ECUs, IoT microcontrollers, and micro-gateways.
2. **Buffering Latency**: To achieve good compression, raw-byte compressors require large blocks of buffered data before outputting compressed frames.
3. **Ignorance of Log Semantics**: In software systems, log lines are never random text. They are produced by programmatic formatting statements:
   ```c
   printf("%s [%s] sensor=%s value=%d status=%s\n", timestamp, level, id, val, status);
   ```

**The LDS Solution**:
LDS separates each log entry into two distinct layers:
- **Static Structural Template**: The constant literal text printed by the code (`"[INFO] sensor=" [VAR] " value=" [VAR]`).
- **Dynamic Typed Variables**: The runtime values (timestamps, numbers, UUIDs, categorical status strings).

By learning templates online and routing variables to specialized mathematical predictors, LDS achieves **8x to 16.2x compression** with an ultra-strict **~166 KB constant memory ceiling** and zero buffering latency.

---

## 2. Architecture & Pipeline Overview

```
                      INCOMING RAW LOG LINE
                                │
                                ▼
  ┌───────────────────────────────────────────────────────────┐
  │ 1. STREAMING TOKENIZER & MINER (miner.c)                  │
  │    • Zero-regex lexical scanner                           │
  │    • Classifies tokens: Ints ('d'), Strings ('s'), Delims │
  │    • Preserves signed negative zeros ('-0.x')             │
  └─────────────────────────────┬─────────────────────────────┘
                                │
                                ▼
  ┌───────────────────────────────────────────────────────────┐
  │ 2. TEMPLATE MATCHER & MRU CACHE (encoder.c)               │
  │    • Fast-path MRU[0] test (O(1) time, ~70% hit rate)     │
  │    • 256-bucket prefix hash table                         │
  │    • Dynamic template generalization (mergeEl)            │
  └─────────────────────────────┬─────────────────────────────┘
                                │
              ┌─────────────────┴─────────────────┐
              ▼                                   ▼
  ┌───────────────────────────┐       ┌───────────────────────────┐
  │ 3A. NUMERIC MODELING      │       │ 3B. STRING & LZ MODELING  │
  │ • Delta: d = v - prev     │       │ • 512-entry RecentStr     │
  │ • Stride: s = d - prev_d  │       │ • LRU Dictionary (24 KB)  │
  │ • Zero error = 1 bit ('0')│       │ • 2-Way LZ Corpus (8 KB)  │
  └─────────────┬─────────────┘       └─────────────┬─────────────┘
                └─────────────────┬─────────────────┘
                                  │
                                  ▼
  ┌───────────────────────────────────────────────────────────┐
  │ 4. ADAPTIVE BINARY RANGE CODER (wire.c)                   │
  │ • Multi-context arithmetic range encoder                  │
  │ • Bit-level optimal entropy packing                       │
  └─────────────────────────────┬─────────────────────────────┘
                                │
                                ▼
                   COMPRESSED WIRE PAYLOAD
```

---

## 3. The 4 Engine Components in Detail

### Component 1: Zero-Regex Streaming Tokenizer & Miner (`miner.c`)
- **Fast Scanning**: Instead of slow regular expressions, `miner.c` uses single-pass character classification.
- **Typed Recognition**:
  - **UUID**: Fast 36-char scanner matching `8-4-4-4-12` hex patterns.
  - **MAC**: Colon/hyphen-separated hardware addresses.
  - **IPv4**: 4-octet dot-delimited addresses.
  - **Signed Integers**: Parsed into 64-bit channels (`'d'`).
- **Edge Case Protection (Signed Zero)**:
  Floating point strings like `-0.15` contain `-0` in the integer channel. Standard two's complement integer math destroys the negative sign (`-0 == 0`). `miner.c` detects this pattern and routes it to categorical string channels (`'s'`) to guarantee **100% bit-exact lossless round-trips**.

### Component 2: Template Matcher & Fast-Path MRU (`encoder.c`, `decoder.c`)
- **MRU[0] Check**: In production logs, consecutive log entries often originate from the same log source. Checking the most recently used template (`enc->mru[0]`) yields an immediate **~70% cache hit rate** in $O(1)$ time without hashing.
- **256-Bucket Prefix Index**: When MRU[0] misses, LDS hashes the first 8 characters into a 256-bucket table, testing only candidate templates that match the prefix.
- **Online Generalization**: If unmatched, the line is compared against recent seeds using longest common subsequence alignment (`mergeEl`) to discover new template candidates on-the-fly.

### Component 3: Predictive Variable Modeler (`model.c`, `utils.c`)
#### A. Numerical Variables (Delta & Stride Prediction)
Let $v_t$ be the numeric variable value at step $t$:
$$\text{Delta: } d_t = v_t - v_{t-1}$$
$$\text{Stride: } s_t = d_t - d_{t-1}$$
$$\text{Predicted Value: } \hat{v}_t = v_{t-1} + d_{t-1}$$
$$\text{Residual: } r_t = v_t - \hat{v}_t$$

- **Exact Prediction Hit ($r_t = 0$)**: Transmitted as a **single bit (`0`)**. For monotonic sequence numbers, constant timestamps, or fixed sensor steps, this reduces a multi-byte integer to 1 bit.
- **Non-zero Residual**: Transmitted as bit `1` followed by variable-length zigzag-encoded residual.

#### B. Categorical Strings & Identifiers
- **RecentStr Ring Buffer**: A fixed 512-slot ring buffer tracks recently seen string tokens. If a token was seen recently, it is encoded as a tiny ring offset (1 byte).
- **LRU Dictionary**: Per-template dictionary with fixed memory caps.
- **2-Way Set-Associative LZ Corpus**: 8 KB fixed-size window for residual substrings.

### Component 4: Adaptive Binary Range Coder (`wire.c`)
- High-speed integer arithmetic range encoder.
- Maintains dynamic context frequencies, adjusting bit costs based on symbol likelihood.
- Emits fully packed, byte-aligned bitstreams without requiring external compression libraries.

---

## 4. Concrete Walkthrough with an Example

Let us trace how LDS processes a realistic log line:

```text
2026-10-05 14:32:10 INFO [Worker-12] Processed 1045 records in 42ms
```

### Scenario A: Single-Line Streaming Mode

#### Step 1: Tokenization (Encoder)
The tokenizer scans the character string and identifies:
- Literal 0: `""` (start of line)
- Variable 1 (Timestamp): `"2026-10-05 14:32:10"` (Type `'s'` or composite date)
- Literal 1: `" INFO [Worker-"`
- Variable 2 (Worker ID): `12` (Type `'d'`, Integer)
- Literal 2: `"] Processed "`
- Variable 3 (Record count): `1045` (Type `'d'`, Integer)
- Literal 3: `" records in "`
- Variable 4 (Duration): `42` (Type `'d'`, Integer)
- Literal 4: `"ms"`

**Template Signature**:
`"<*> INFO [Worker-<*>] Processed <*> records in <*>ms"`

#### Step 2: Template Matching
- **Encoder Checks MRU[0]**: Does the previous log match this template?
  - If **YES** (Hit): Template ID is encoded with a 1-bit confirmation flag.
  - If **NO**: Prefix hash table is checked; if known, Template ID (e.g., ID `7`) is emitted.
  - If completely new: A definition packet (`OP_DEFINE`) is emitted once to synchronize the decoder.

#### Step 3: Predictive Variable Encoding
Suppose this is the 100th log of this template:
- **Timestamp**: Predicted from previous interval (e.g., +1 second delta). Residual $= 0 \implies \mathbf{1\text{ bit}}$.
- **Worker ID (`12`)**: Previous was `12`. Delta $= 0 \implies \mathbf{1\text{ bit}}$.
- **Record count (`1045`)**: Previous was `1040` (delta $+5$), current is `1045` (delta $+5$). Stride $= 0 \implies \mathbf{1\text{ bit}}$.
- **Duration (`42`)**: Small delta from previous value `40` (residual $+2$) $\implies \mathbf{4\text{ bits}}$.

#### Step 4: Wire Serialization
The range coder serializes the template reference and variable bits:
- Raw line length: **68 bytes**
- Compressed packet size: **~8 to 12 bytes**
- **Effective Compression: 5.6x to 8.5x on a single isolated line!**

#### Step 5: Decoding (Receiver)
1. The decoder receives the 10-byte wire packet.
2. Reads the header flag $\implies$ Identifies Template ID `7`.
3. Looks up Template `7`: `"<*> INFO [Worker-<*>] Processed <*> records in <*>ms"`.
4. Decodes variables from the bitstream using its matching predictive state:
   - Evaluates timestamp prediction $\to$ reconstructs `"2026-10-05 14:32:10"`.
   - Evaluates worker delta $\to$ `12`.
   - Evaluates records stride $\to$ `1045`.
   - Evaluates duration residual $\to$ `42`.
5. Interleaves literals and decoded variables into the output buffer:
   ```text
   2026-10-05 14:32:10 INFO [Worker-12] Processed 1045 records in 42ms
   ```
6. Result: **100% bit-exact match with original input.**

---

### Scenario B: Batched Block Mode (16 KB Transport Blocks)

In batched mode, lines are accumulated into a buffer until reaching a target byte threshold (e.g., 16,384 bytes):

```
[Raw Log 1] ──┐
[Raw Log 2] ──┼──> [16 KB Block Buffer] ──> [LDS Block Encoder] ──> [Single Compressed Packet (~1 KB)]
[Raw Log N] ──┘
```

#### Why Batched Mode Achieves Even Higher Ratios (up to 48x):
1. **Header Amortization**: Network protocols (TCP, HTTP, MQTT) add 20 to 60 bytes of overhead per packet. In batched mode, one single network packet transports hundreds of log lines.
2. **Template Re-use within the Block**: Templates defined at the start of the block are referenced with minimal 1-byte indices throughout the rest of the block.
3. **Continuous Delta Streaks**: Numerical variables maintain tighter delta-stride correlation across continuous lines within the block.

---

## 5. Algorithmic Complexity & Memory Bounds

| Metric | Single-Line Mode | Batched Block Mode | Traditional Gzip | Traditional Zstandard |
| :--- | :--- | :--- | :--- | :--- |
| **Time Complexity** | $O(L)$ per line | $O(N \cdot L)$ per block | $O(M)$ | $O(M)$ |
| **Peak RAM Ceiling** | **~166 KB (Strict)** | **~180 KB** | $300\text{ KB} - 1\text{ MB}$ | $1\text{ MB} - 32\text{ MB}+$ |
| **Heap Dynamic Alloc** | **Zero during streaming** | **Zero during streaming** | Dynamic hash tables | Dynamic context memory |
| **Buffering Latency** | **0 ms (Immediate)** | Configurable (e.g., 16 KB) | High (wait for block) | High (wait for block) |
| **Log Compression** | **8x – 16.2x** | **12x – 48.4x** | 3x – 6x | 4x – 8x |
