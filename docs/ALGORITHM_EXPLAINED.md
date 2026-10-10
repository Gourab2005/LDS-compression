# 📘 LDS Compression Algorithm — The Complete, Easy-to-Understand Guide

> **Purpose of this document:** This guide explains the LDS (Log Data Stream) compression algorithm in a simple, visual way — so that you can understand it deeply and explain it to others (professors, interviewers, teammates) with full confidence.

---

## 📌 Table of Contents

1. [The Core Idea (Why This Works)](#1-the-core-idea-why-this-works)
2. [The Big Picture — 4-Stage Pipeline](#2-the-big-picture--4-stage-pipeline)
3. [Stage 1: Tokenizer & Miner (miner.c)](#3-stage-1-tokenizer--miner-minerc)
4. [Stage 2: Template Matching (encoder.c)](#4-stage-2-template-matching-encoderc)
5. [Stage 3: Predictive Modeling (model.c + utils.c)](#5-stage-3-predictive-modeling-modelc--utilsc)
6. [Stage 4: Range Coder (wire.c)](#6-stage-4-range-coder-wirec)
7. [How Decoding Works (decoder.c)](#7-how-decoding-works-decoderc)
8. [Complete End-to-End Example](#8-complete-end-to-end-example)
9. [File-by-File Breakdown](#9-file-by-file-breakdown)
10. [Input / Output Specification](#10-input--output-specification)
11. [Memory Budget Breakdown](#11-memory-budget-breakdown)
12. [Key Design Decisions & Why](#12-key-design-decisions--why)
13. [Glossary of Terms](#13-glossary-of-terms)

---

## 1. The Core Idea (Why This Works)

### The Observation

Log lines are **NOT random text**. They come from `printf`-style statements in code:

```c
// The programmer wrote THIS code:
printf("%s [INFO] sensor=%s speed=%d km/h\n", timestamp, sensor_id, speed);
```

This produces output like:
```
2026-10-05 14:32:10 [INFO] sensor=TMP01 speed=45 km/h
2026-10-05 14:32:11 [INFO] sensor=TMP01 speed=46 km/h
2026-10-05 14:32:12 [INFO] sensor=TMP01 speed=47 km/h
```

**Notice the pattern:**
- 🟦 **The structure stays the same** every time → `"[INFO] sensor=___ speed=___ km/h"`
- 🟧 **Only the values change** → timestamps, sensor ID, speed number

### The Breakthrough

Instead of compressing raw characters byte-by-byte (like Gzip does), LDS says:

> "I'll learn the **template** (the static skeleton) once, give it an **ID number**, and then for each new log line, I only need to transmit the **ID + the changed values**."

And for the values themselves:

> "If the speed was 45 last time and it's 46 now, the **difference is just +1**. I can encode that difference in 1-4 bits instead of sending the full number."

**This is why LDS gets 10-16x compression while Gzip only gets 3-5x on logs.**

---

## 2. The Big Picture — 4-Stage Pipeline

Every log line passes through **4 stages** to get compressed:

```
  ┌─────────────────────────────────────────────────────────────────────────┐
  │                         RAW LOG LINE (Input)                            │
  │  "2026-10-05 14:32:10 INFO [Worker-12] Processed 1045 records in 42ms" │
  └───────────────────────────────────┬─────────────────────────────────────┘
                                      │
                                      ▼
  ┌───────────────────────────────────────────────────────────────────┐
  │  STAGE 1: TOKENIZER (miner.c)                                    │
  │  Break the line into tokens and classify each one                 │
  │  → Numbers, Strings, UUIDs, IPs, Punctuation, Words              │
  └───────────────────────────────────┬─────────────────────────────────┘
                                      │
                                      ▼
  ┌───────────────────────────────────────────────────────────────────┐
  │  STAGE 2: TEMPLATE MATCHER (encoder.c)                            │
  │  Find or create a template that matches this line's structure     │
  │  → Template ID = just 1 byte!                                     │
  └───────────────────────────────────┬─────────────────────────────────┘
                                      │
                          ┌───────────┴───────────┐
                          ▼                       ▼
  ┌─────────────────────────────┐   ┌─────────────────────────────┐
  │  STAGE 3A: Number Predictor │   │  STAGE 3B: String Predictor │
  │  (model.c)                  │   │  (model.c + utils.c)        │
  │  Delta-Stride prediction    │   │  Ring buffer + LZ matching  │
  │  on numeric values          │   │  on string values           │
  └──────────────┬──────────────┘   └──────────────┬──────────────┘
                 └───────────┬─────────────────────┘
                             ▼
  ┌───────────────────────────────────────────────────────────────────┐
  │  STAGE 4: RANGE CODER (wire.c)                                    │
  │  Pack all the bits into a tight binary payload                    │
  │  → Adaptive arithmetic coding for near-optimal bit packing       │
  └───────────────────────────────────┬─────────────────────────────────┘
                                      │
                                      ▼
  ┌─────────────────────────────────────────────────────────────────────────┐
  │                       COMPRESSED PACKET (Output)                        │
  │                      Only ~8-15 bytes!  (was 68 bytes)                  │
  └─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Stage 1: Tokenizer & Miner (`miner.c`)

### What it does

The tokenizer **reads the raw log line character by character** (a single pass, left to right) and breaks it into a list of **typed tokens**.

### How it classifies tokens

The scanner checks each position in the string, in this priority order:

| Priority | Type | How it Detects | Example | Flag |
|:---:|:---|:---|:---|:---:|
| 1 | **UUID** | Exactly 36 chars, pattern `8-4-4-4-12` hex | `550e8400-e29b-41d4-a716-446655440000` | `2` (string var) |
| 2 | **MAC Address** | 17 chars, colon/hyphen separated hex pairs | `AA:BB:CC:DD:EE:FF` | `2` (string var) |
| 3 | **IPv4 Address** | 4 dot-separated octets (0-255) | `192.168.1.100` | `2` (string var) |
| 4 | **Integer** | Sequence of digits, optionally negative | `1045`, `-7` | `1` (numeric var) |
| 5 | **Word** | Alphabetic/alphanumeric sequence | `INFO`, `Worker` | `0` (literal) |
| 6 | **Single char** | Any other character (punctuation, space) | `[`, `]`, ` ` | `0` (literal) |

### The Flag System

Each token gets a **flag** that tells the encoder what kind of variable it is:

```
Flag 0 → "Literal / static text"    (becomes part of the template skeleton)
Flag 1 → "Integer variable" ('d')   (gets delta-stride prediction)
Flag 2 → "String variable"  ('s')   (gets string dictionary lookup)
```

### Example: Tokenizing a Real Log Line

**Input:**
```
2026-10-05 14:32:10 INFO [Worker-12] Processed 1045 records in 42ms
```

**Token breakdown:**

| Token | Type | Flag | Why |
|:---|:---|:---:|:---|
| `2026` | Integer | 1 | Digits only |
| `-` | Punctuation | 0 | Single character |
| `10` | Integer | 1 | Digits only |
| `-` | Punctuation | 0 | Single character |
| `05` | Integer | 1 | Digits only |
| ` ` | Space | 0 | Single character |
| `14` | Integer | 1 | Digits only |
| `:` | Punctuation | 0 | Single character |
| `32` | Integer | 1 | Digits only |
| `:` | Punctuation | 0 | Single character |
| `10` | Integer | 1 | Digits only |
| ` ` | Space | 0 | Single character |
| `INFO` | Word | 0 | Pure alphabetic, short → literal |
| ` ` | Space | 0 | Single character |
| `[` | Punctuation | 0 | Single character |
| `Worker` | Word | 0 | Pure alphabetic → literal |
| `-` | Punctuation | 0 | Single character |
| `12` | Integer | 1 | Digits only |
| `]` | Punctuation | 0 | Single character |
| ` ` | Space | 0 | Single character |
| `Processed` | Word | 0 | Pure alphabetic → literal |
| ` ` | Space | 0 | Single character |
| `1045` | Integer | 1 | Digits only |
| ` ` | Space | 0 | Single character |
| `records` | Word | 0 | Pure alphabetic → literal |
| ` ` | Space | 0 | Single character |
| `in` | Word | 0 | Pure alphabetic → literal |
| ` ` | Space | 0 | Single character |
| `42` | Integer | 1 | Digits only |
| `ms` | Word | 0 | Pure alphabetic → literal |

### Special Case: Negative Zero (`-0`)

The integer `-0` is tricky: in computer math, `-0 == 0`, so the minus sign would be lost. The tokenizer **detects** patterns like `-0.15` and routes them as **string variables** instead of integers to guarantee the exact original text is preserved on decompression.

### Key Takeaway

> **The tokenizer turns a raw text string into a structured list of typed tokens — separating "what changes" from "what stays the same."**

---

## 4. Stage 2: Template Matching (`encoder.c`)

### What is a Template?

A template is the **skeleton of a log line** — the static parts with `<*>` placeholders where variables go.

**Log line:**
```
2026-10-05 14:32:10 INFO [Worker-12] Processed 1045 records in 42ms
```

**Its template:**
```
<*>-<*>-<*> <*>:<*>:<*> INFO [Worker-<*>] Processed <*> records in <*>ms
```

Each template gets a unique **Template ID** (e.g., ID `7`), which is just a small number (0-255).

### The 3-Level Lookup Strategy

The encoder uses a clever 3-level strategy to match incoming lines to templates:

```
                    New log line arrives
                           │
                           ▼
               ┌───────────────────────┐
         ┌──── │ Level 1: MRU[0] Check │ ────── Hit? (~70% of the time)
         │     │ "Is it the SAME        │        → Encode Template ID
         │     │  template as the       │          with 1-bit flag "yes"
         │     │  LAST line?"           │
         │     └───────────┬───────────┘
         │                 │ Miss
         │                 ▼
         │     ┌───────────────────────┐
         │     │ Level 2: Hash Table   │ ────── Hit?
         │     │ 256-bucket prefix     │        → Encode Template ID
         │     │ index (first 8 chars) │
         │     └───────────┬───────────┘
         │                 │ Miss
         │                 ▼
         │     ┌───────────────────────┐
         │     │ Level 3: Create New   │ ────── New template!
         │     │ Mine from recent seed │        → Emit OP_DEFINE packet
         │     │ lines using mergeEl() │          to teach decoder
         │     └───────────────────────┘
         │
         ▼
    TEMPLATE ID FOUND
    (now encode only the variable values)
```

#### Level 1: MRU[0] — The "Same as Last" Check

In real systems, the same log source fires repeatedly. The last-used template is checked **first** — this is an O(1) check and hits **~70% of the time**.

#### Level 2: 256-Bucket Prefix Hash Table

When MRU[0] misses, LDS hashes the **first 8 characters** of the line and looks up candidate templates in a hash table. Only matching candidates are tested, making this very fast.

#### Level 3: Online Template Mining

When no existing template matches, LDS compares the line to **seed lines** it has saved. It uses a **Longest Common Subsequence (LCS) alignment** algorithm (`mergeEl`) to figure out which tokens are static (same in both) and which are variable (different).

### Wire Opcodes

The encoder emits one of these opcodes for each line:

| Opcode | Name | When Used | What's Sent |
|:---:|:---|:---|:---|
| `0` | `OP_DATA` | Template matched! | Template ID + variable values |
| `1` | `OP_RAW` | No template found | Raw text (fallback, compressed via LZ) |
| `2` | `OP_SYNC` | New template learned | Full template definition (literals + kinds) |
| `3` | `OP_REDEF` | Template updated | Updated template definition |

### Key Takeaway

> **The template matcher turns a 68-byte log line into a 1-byte Template ID + a few tiny variable values. This is where the magic of high compression begins.**

---

## 5. Stage 3: Predictive Modeling (`model.c` + `utils.c`)

This is the **heart** of LDS's compression power. Once the template is identified, each variable slot is compressed using smart prediction.

### 5A. Numeric Variables — Delta-Stride Prediction

#### The Idea

Instead of encoding the full number, encode **how much it changed** from what we predicted.

#### Step-by-Step Math

Given a sequence of values for the same variable slot across consecutive log lines:

```
v₁ = 1030,  v₂ = 1035,  v₃ = 1040,  v₄ = 1045
```

**Delta** = Current value − Previous value:
```
d₂ = 1035 - 1030 = +5
d₃ = 1040 - 1035 = +5
d₄ = 1045 - 1040 = +5
```

**Stride** = Current delta − Previous delta:
```
s₃ = d₃ - d₂ = 5 - 5 = 0
s₄ = d₄ - d₃ = 5 - 5 = 0
```

**Prediction** = Previous value + Previous delta:
```
predicted v₄ = v₃ + d₃ = 1040 + 5 = 1045
```

**Residual** = Actual − Predicted:
```
r₄ = v₄ - predicted v₄ = 1045 - 1045 = 0
```

#### How the Residual is Encoded

```
  Residual = 0?
      │
      ├── YES → Send just 1 BIT: '0'  ← This is INCREDIBLY cheap!
      │
      └── NO  → Send '1' + zigzag-encoded residual value
                 (small residuals = few bits)
```

#### Why This is Powerful

| Scenario | What Happens | Bits Needed |
|:---|:---|:---:|
| Counter increments by 1 every line | Delta = 1, Stride = 0 → Residual = 0 | **1 bit** |
| Timestamp adds 1 second each line | Delta = 1, Stride = 0 → Residual = 0 | **1 bit** |
| Sensor reading stays constant | Delta = 0, Stride = 0 → Residual = 0 | **1 bit** |
| Value changes unpredictably by ±3 | Residual = ±3 | **~4-6 bits** |
| Completely random value | Full value encoded | **~20-40 bits** |

> **Key insight: In real logs, 50-80% of numeric variables have a residual of 0, so they cost only 1 bit each!**

### 5B. String Variables — Dictionary + LZ Matching

String values (sensor names, log levels, UUIDs, etc.) are handled differently:

```
  String variable arrives (e.g., "TMP01")
      │
      ├── Check 1: Is it the SAME as the predicted value?
      │   └── YES → Send 1 BIT: '0'  (prediction hit!)
      │
      ├── Check 2: Is it in the RecentStr ring buffer (512 slots)?
      │   └── YES → Send ring offset (~9 bits)
      │
      ├── Check 3: Is it in the LRU Dictionary (24 KB)?
      │   └── YES → Send dictionary rank (~8-12 bits)
      │
      ├── Check 4: Is it in the LZ Corpus (8 KB sliding window)?
      │   └── YES → Send (offset, length) match
      │
      └── Fallback: Send raw string bytes
```

#### The RecentStr Ring Buffer

A **fixed 512-slot circular buffer** that remembers recently seen strings. If `"TMP01"` was seen 3 entries ago, it can be referenced as "go back 3 slots" — just a tiny index.

#### The LRU Dictionary

A **Least Recently Used** dictionary (capped at 24 KB total) that tracks per-template, per-slot string values. Frequently used strings get small rank numbers.

#### The 2-Way Set-Associative LZ Corpus

An **8 KB sliding window** that acts like a mini byte-level compressor for any strings that aren't in the ring or dictionary. It finds matching substrings from recent data and encodes them as `(offset, length)` pairs.

### Key Takeaway

> **The predictive modeler turns multi-byte values into 1-bit "same as predicted" flags most of the time. This is what gives LDS its 10-16x compression ratio.**

---

## 6. Stage 4: Range Coder (`wire.c`)

### What it does

The range coder is the **final packing stage**. It takes all the bits and symbols from Stages 2 and 3 and encodes them into a **near-optimal binary byte stream**.

### How it works (Simplified)

Think of it like this:

```
Traditional encoding:                    Range coding:
  Each symbol = fixed bits               Each symbol = variable bits
  "0" always = 1 bit                     "0" if very likely = 0.1 bits
  "1" always = 1 bit                     "1" if unlikely = 3.5 bits

  Result: wastes bits on                 Result: bits perfectly match
          common symbols                          actual probabilities
```

#### Adaptive Probability

The range coder **learns** which symbols are common and which are rare as it processes data:

- If prediction hits (`residual = 0`) happen 90% of the time, the "0" bit costs only ~0.15 bits
- If prediction misses (`residual ≠ 0`) are rare (10%), the "1" bit costs more — but that's fine because it's rare

#### Multi-Context Modeling

Different variable slots have **different probabilities**. The range coder maintains **separate probability tables** for:

- Template hit/miss flags
- Numeric prediction hits
- String prediction hits
- Residual magnitudes

Each context adapts independently, maximizing compression efficiency.

### Key Takeaway

> **The range coder squeezes out the last few percentage points of redundancy, packing the already-small data from the predictor into a mathematically near-optimal byte stream.**

---

## 7. How Decoding Works (`decoder.c`)

Decoding is the **mirror image** of encoding. The decoder must reconstruct the **exact original text, byte-for-byte**.

```
  COMPRESSED PACKET arrives
         │
         ▼
  ┌──────────────────────────┐
  │ Read Opcode:             │
  │  OP_DATA? → Step A       │
  │  OP_RAW?  → Step B       │
  │  OP_SYNC? → Step C       │
  └──────────┬───────────────┘
             │
             ▼ (OP_DATA path — most common)
  ┌──────────────────────────┐
  │ 1. Read Template ID      │
  │ 2. Look up template      │
  │    skeleton + slot kinds  │
  └──────────┬───────────────┘
             │
             ▼
  ┌──────────────────────────┐
  │ 3. For each variable     │
  │    slot in the template: │
  │                          │
  │    Numeric ('d') slot:   │
  │    → Read prediction bit │
  │    → If 0: use predicted │
  │    → If 1: read residual │
  │      and add to predicted│
  │                          │
  │    String ('s') slot:    │
  │    → Read prediction bit │
  │    → If 0: use predicted │
  │    → If 1: read from     │
  │      ring/dict/LZ/raw    │
  └──────────┬───────────────┘
             │
             ▼
  ┌──────────────────────────┐
  │ 4. Reconstruct the line: │
  │    Interleave template   │
  │    literals + decoded    │
  │    variable values       │
  └──────────┬───────────────┘
             │
             ▼
  ORIGINAL LOG LINE (exact match!)
```

### Critical Rule: State Synchronization

The decoder maintains **the exact same internal state** as the encoder:

- Same template table
- Same delta/stride predictors for each numeric slot
- Same string ring buffers and dictionaries
- Same range coder probability tables

This is why the encoder sends `OP_SYNC` packets when it learns a new template — so the decoder can stay in sync.

---

## 8. Complete End-to-End Example

Let's trace **3 consecutive log lines** through the entire system.

### Input Lines

```
Line 1: 2026-10-05 14:32:10 INFO [Worker-12] Processed 1040 records in 40ms
Line 2: 2026-10-05 14:32:11 INFO [Worker-12] Processed 1045 records in 41ms
Line 3: 2026-10-05 14:32:12 INFO [Worker-12] Processed 1050 records in 42ms
```

### Line 1 Processing

**Stage 1 (Tokenize):**
```
Tokens: [2026, -, 10, -, 05, ' ', 14, :, 32, :, 10, ' ', INFO, ' ', [, Worker, -, 12, ], ' ', Processed, ' ', 1040, ' ', records, ' ', in, ' ', 40, ms]
Flags:  [ 1,   0, 1,  0,  1,  0,   1, 0,  1, 0,  1,  0,   0,  0, 0,   0,    0,  1, 0, 0,     0,     0,   1,  0,    0,   0,  0, 0,  1, 0 ]
```

**Stage 2 (Template):**
- First time seeing this pattern → New template created!
- Template: `<*>-<*>-<*> <*>:<*>:<*> INFO [Worker-<*>] Processed <*> records in <*>ms`
- Kinds: `d d d d d d d d d` (9 integer slots)
- Opcode: `OP_SYNC` (teach the decoder this template)

**Stage 3 (Predict):**
- No previous values → All numbers sent in full
- Total: relatively large packet (~30-40 bytes)

**Stage 4 (Range code):**
- Packed into wire payload

---

### Line 2 Processing

**Stage 2 (Template):** MRU[0] hit! Same template → Template ID in 1 bit

**Stage 3 (Predict each variable):**

| Slot | Previous | Current | Predicted | Residual | Bits |
|:---|:---:|:---:|:---:|:---:|:---:|
| Year (`2026`) | 2026 | 2026 | 2026 | **0** | **1 bit** |
| Month (`10`) | 10 | 10 | 10 | **0** | **1 bit** |
| Day (`05`) | 05 | 05 | 05 | **0** | **1 bit** |
| Hour (`14`) | 14 | 14 | 14 | **0** | **1 bit** |
| Minute (`32`) | 32 | 32 | 32 | **0** | **1 bit** |
| Second | 10 | 11 | 11 (delta=+1) | **0** | **1 bit** |
| Worker ID | 12 | 12 | 12 | **0** | **1 bit** |
| Records | 1040 | 1045 | 1045 (delta=+5) | **0** | **1 bit** |
| Duration | 40 | 41 | — | **+1** | **~4 bits** |

**Wait — how does the second prediction work for "Records"?**

- After Line 1: `prev = 1040`, `delta = unknown`
- Line 2 value: `1045` → delta = `+5` → Since no prior delta exists, the predicted value = `1040 + 0 = 1040`, so residual = `+5` → encoded as small zigzag

Actually on Line 2, the deltas are being **learned** for the first time. The real savings kick in on Line 3.

---

### Line 3 Processing

**Stage 2:** MRU[0] hit again → 1 bit

**Stage 3 (Predict):**

| Slot | prev | prev_delta | Predicted | Actual | Residual | Bits |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| Second | 11 | +1 | **12** | 12 | **0** | **1 bit** |
| Worker ID | 12 | 0 | **12** | 12 | **0** | **1 bit** |
| Records | 1045 | +5 | **1050** | 1050 | **0** | **1 bit** |
| Duration | 41 | +1 | **42** | 42 | **0** | **1 bit** |

**Total for Line 3:** Template (1 bit) + 9 prediction bits ≈ **~10 bits ≈ 2 bytes!**

The original line was **68 bytes**. That's **~34x compression on a single line!**

---

## 9. File-by-File Breakdown

### Core Source Files (`src/`)

| File | Role | What It Does | Key Data Structures |
|:---|:---|:---|:---|
| **lds.c** | Public API | Entry point. Wraps internal encoder/decoder into the clean `lds_encode_line()` / `lds_decode_line()` public API | `LDSEncoder`, `LDSDecoder` |
| **config.c** | Configuration | Defines all memory budgets and tuning parameters | `SharedConfig`, `EncOnlyConfig` |
| **miner.c** | Stage 1: Tokenizer | Zero-regex character scanner. Breaks log lines into typed tokens. Handles UUID, MAC, IPv4, integers, and edge cases like `-0` | `LexResult` (token array + flags) |
| **encoder.c** | Stage 2: Encoder | Template matching with MRU cache + prefix hash table. Online template mining via LCS alignment. Emits wire opcodes | `Encoder`, `Tpl` (template), MRU array |
| **model.c** | Stage 3: Modeler | Delta-stride numeric prediction. String ring buffers (RecentStr/RecentNum). LRU dictionary management. Template definition and matching | `Model`, `Tpl`, `RecentNum`, `RecentStr`, `DictEntry` |
| **utils.c** | Stage 3: Utilities | LZ corpus (sliding window byte compressor). Zigzag encoding. Variable-length integer encoding (varint). Fast hashing | `Corpus` (2-way set-associative LZ) |
| **wire.c** | Stage 4: Coder | Adaptive binary arithmetic range coder. Multi-context probability tables. Bit-level optimal entropy packing | `Coder`, `Wire`, probability arrays |
| **decoder.c** | Decoder | Reverse of encoder. Reads wire packets, reconstructs template + variables, outputs exact original text | `Decoder` |

### Public Header (`include/`)

| File | Purpose |
|:---|:---|
| **lds.h** | The complete public C99 API. Exposes `lds_encoder_create()`, `lds_encode_line()`, `lds_decode_line()`, etc. |

### Configuration Defaults (`config.c`)

```c
SharedConfig SHARED = {
    .maxDictBytes  = 24000,   // 24 KB string dictionary budget
    .maxEntryBytes = 64,      // Max 64 bytes per dictionary entry
    .corpusWin     = 8192,    // 8 KB LZ corpus sliding window
    .maxTemplates  = 256,     // Up to 256 active templates
};

EncOnlyConfig ENC_ONLY = {
    .simExtend  = 0.75,       // Similarity threshold for template extension
    .simSeed    = 0.65,       // Similarity threshold for seed matching
    .maxRedefs  = 32,         // Max times a template can be redefined
    .maxSeeds   = 16,         // Max seed lines kept for mining
    .maxTry     = 32,         // Max candidates to test during matching
    .maxLineLen = 16384,      // Max input line length (16 KB)
};
```

### Example Programs (`examples/`)

| File | What It Demonstrates |
|:---|:---|
| **01_single_line_stream.c** | Compresses and decompresses logs **one line at a time** in real-time |
| **02_batch_stream.c** | Accumulates lines into 16 KB blocks for higher compression |
| **03_tcp_sender.c** | Edge device: reads logs, compresses, sends over TCP socket |
| **04_tcp_receiver.c** | Cloud server: receives TCP packets, decompresses, prints original logs |

### Integration Wrappers (`integration/`)

| File | Language | Purpose |
|:---|:---|:---|
| **lds_client.py** | Python | Spawns the encoder/decoder engines as subprocesses, communicates via JSON over stdin/stdout |
| **lds_client.js** | Node.js | Same approach for JavaScript applications |

---

## 10. Input / Output Specification

### Encoder Input

| Property | Details |
|:---|:---|
| **Format** | Null-terminated C string (one log line) |
| **Max Length** | 16,384 bytes (configurable via `maxLineLen`) |
| **Encoding** | ASCII / UTF-8 (byte-transparent) |
| **Line Ending** | Stripped before processing (no `\n` or `\r\n`) |

### Encoder Output (Compressed Packet)

| Property | Details |
|:---|:---|
| **Format** | Binary byte array (`uint8_t*`) |
| **Typical Size** | 8 – 15 bytes per line (single-line mode) |
| **Contains** | Opcode + Template ID + encoded variable values |
| **Self-contained?** | Yes — each packet can be decoded independently if both encoder and decoder have synchronized state |

### Decoder Input

| Property | Details |
|:---|:---|
| **Format** | Binary byte array (the compressed packet from the encoder) |
| **Requirement** | Decoder must have processed all previous packets in order (stateful) |

### Decoder Output

| Property | Details |
|:---|:---|
| **Format** | Null-terminated C string |
| **Guarantee** | **100% bit-exact match** with the original input string |
| **Verification** | `strcmp(original, decoded) == 0` always holds |

### C API Usage Example

```c
#include "lds.h"

// === SENDER (Edge Device) ===
LDSEncoder* enc = lds_encoder_create();        // ~166 KB RAM
size_t compressed_len = 0;

// Compress one line
uint8_t* packet = lds_encode_line(enc, 
    "2026-10-05 14:32:10 INFO speed=45.2km/h", 
    &compressed_len);

// Send 'packet' (compressed_len bytes) over network...
send(socket_fd, packet, compressed_len, 0);
free(packet);
lds_encoder_free(enc);

// === RECEIVER (Cloud Server) ===
LDSDecoder* dec = lds_decoder_create();        // ~166 KB RAM

// Decompress
char* original = lds_decode_line(dec, packet, packet_len);
printf("%s\n", original);  // Prints exact original line
free(original);
lds_decoder_free(dec);
```

### Batch Mode API

```c
// Compress multiple lines at once (higher compression ratio)
const char* lines[] = {
    "2026-10-05 14:32:10 INFO speed=45km/h",
    "2026-10-05 14:32:11 INFO speed=46km/h",
    "2026-10-05 14:32:12 INFO speed=47km/h",
};

size_t block_len;
uint8_t* block = lds_encode_block(enc, lines, 3, &block_len);

// Decompress
size_t num_lines;
char** decoded = lds_decode_block(dec, block, block_len, &num_lines);

for (size_t i = 0; i < num_lines; i++) {
    printf("%s\n", decoded[i]);  // Exact original lines
}

lds_free_lines(decoded, num_lines);
free(block);
```

---

## 11. Memory Budget Breakdown

LDS operates within a strict **~166 KB constant RAM ceiling**. Here's where every byte goes:

```
┌─────────────────────────────────────────────────────────────────┐
│                    TOTAL: ~166 KB                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ Template Table       │  ~40 KB                               │
│  │ (256 templates ×     │  - Template literals (skeletons)      │
│  │  ~160 bytes each)    │  - Per-slot delta/stride state        │
│  └──────────────────────┘  - Per-slot probability tables        │
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ RecentNum Ring       │  ~36 KB                               │
│  │ (512 slots + 2048    │  - Numeric value history ring         │
│  │  hash index)         │  - Hash map for fast lookup           │
│  └──────────────────────┘                                       │
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ RecentStr Ring       │  ~28 KB                               │
│  │ (512 slots + 2048    │  - String value history ring          │
│  │  hash index)         │  - Hash map for fast lookup           │
│  └──────────────────────┘                                       │
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ LRU Dictionary       │  ~24 KB                               │
│  │ (maxDictBytes=24000) │  - Per-slot string dictionaries       │
│  └──────────────────────┘  - Doubly-linked LRU list             │
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ LZ Corpus            │  ~24 KB                               │
│  │ (8 KB window ×2      │  - Sliding window for byte matching   │
│  │  + hash index)       │  - 2-way set-associative hash index   │
│  └──────────────────────┘                                       │
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ Wire / Range Coder   │  ~8 KB                                │
│  │ (probability tables  │  - Context-adaptive probability model │
│  │  + output buffer)    │  - Bit output accumulator             │
│  └──────────────────────┘                                       │
│                                                                 │
│  ┌──────────────────────┐                                       │
│  │ Prefix Index + MRU   │  ~6 KB                                │
│  │ (256 buckets × 8     │  - Template lookup acceleration       │
│  │  + MRU array)        │  - Most Recently Used list            │
│  └──────────────────────┘                                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

> **Why this matters:** This fits comfortably in the RAM of a $2 microcontroller. Gzip needs 300 KB – 1 MB. Zstandard needs 1 MB – 32 MB.

---

## 12. Key Design Decisions & Why

| Decision | Why |
|:---|:---|
| **No regex for tokenization** | Regex engines are slow and need backtracking. The single-pass character scanner in `miner.c` is 10-50x faster |
| **Fixed memory ceiling** | Edge devices (ECUs, IoT) can't afford dynamic memory growth. Every data structure has a hard cap |
| **MRU[0] first** | In real logs, 70% of lines use the same template as the previous line. Checking it first avoids all hash table work |
| **Delta-stride (2nd order)** | Simple delta (1st order) catches counters. Adding stride (2nd order) also catches accelerating sequences and constant-step patterns |
| **Zigzag encoding** | Small negative numbers like `-1` would need 8 bytes in two's complement. Zigzag maps `-1 → 1`, `-2 → 3`, keeping small values small |
| **2-way set-associative LZ** | A full hash chain (like Gzip uses) needs `O(window_size)` memory for the chain array. 2-way associativity uses only 2 slots per bucket — constant memory |
| **Range coder (not Huffman)** | Huffman codes are integer-bit (1, 2, 3 bits). Range coding can use fractional bits (0.1, 1.7 bits), achieving better compression |
| **Separate encoder/decoder state** | Allows the encoder to run on an edge device and decoder on a cloud server — they sync state through `OP_SYNC` packets |

---

## 13. Glossary of Terms

| Term | Meaning |
|:---|:---|
| **Template** | The static skeleton of a log line with `<*>` variable placeholders |
| **Template ID** | A small integer (0-255) identifying a learned template |
| **MRU** | Most Recently Used — a cache of recently used templates |
| **Delta** | The difference between the current and previous value of a numeric variable |
| **Stride** | The difference between the current and previous delta (second-order derivative) |
| **Residual** | The difference between the actual value and the predicted value |
| **Zigzag encoding** | A way to map signed integers to unsigned: 0→0, -1→1, 1→2, -2→3, 2→4, etc. |
| **Range coder** | An arithmetic compression algorithm that encodes symbols using fractional bits |
| **Corpus** | A sliding window of recent bytes used for LZ-style substring matching |
| **LRU** | Least Recently Used — an eviction strategy for bounded-memory dictionaries |
| **Ring buffer** | A fixed-size circular array where new entries overwrite the oldest |
| **Varint** | Variable-length integer encoding where small numbers use fewer bytes |
| **Opcode** | A tag byte identifying what type of packet follows (DATA, RAW, SYNC, REDEF) |
| **Lossless** | The decompressed output is a bit-exact match of the original input |
| **Online** | Processing happens one item at a time as data arrives, not in retrospect |

---

> **💡 Summary for explaining to others:**
> 
> *"LDS exploits the fact that logs come from printf-style code — so the structure repeats. It learns the repeating template, assigns it a tiny ID, and then for each new line only sends the ID plus the changed values. For numbers, it predicts the next value using delta-stride math, and if the prediction is right, it costs only 1 bit. For strings, it uses a ring buffer and dictionary. Finally, an arithmetic range coder packs everything into a near-optimal binary stream. The entire engine runs in just 166 KB of RAM — small enough for a microcontroller."*
