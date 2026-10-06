#!/usr/bin/env python3
"""
LDS (Log Data Stream) - Python Streaming Client
================================================
Demonstrates how to integrate the native LDS C compression engine into
Python applications via standard I/O streaming pipes (zero IPC overhead).

Requirements:
    - Precompiled lds_encoder_engine.exe (or Linux binary) in ../../../bin/
"""

import os
import sys
import json
import subprocess

def get_engine_path():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(base_dir, "..", "..", "..", "bin", "lds_encoder_engine.exe"),
        os.path.join(base_dir, "..", "..", "..", "bin", "lds_encoder_engine"),
        os.path.join(base_dir, "..", "..", "..", "docs", "downloads", "lds_encoder_engine.exe"),
        os.path.join(base_dir, "..", "..", "..", "docs", "downloads", "lds_encoder_engine"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise FileNotFoundError("Could not locate lds_encoder_engine binary.")

def main():
    engine_path = get_engine_path()
    print(f"[*] Starting LDS Native Engine: {engine_path}")

    # Launch LDS engine with unbuffered stdio pipe
    proc = subprocess.Popen(
        [engine_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    # Read greeting
    greeting = proc.stdout.readline()
    print(f"[*] Engine Status: {greeting.strip()}")

    sample_logs = [
        "2026-10-05 02:01:47.520 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=41.7m speed=45.2km/h status=OK",
        "2026-10-05 02:01:47.620 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.9m speed=45.1km/h status=OK",
        "2026-10-05 02:01:47.720 INFO ADAS [RADAR] sensor=FRONT objects=5 dist=40.1m speed=44.9km/h status=OK",
        "2026-10-05 02:01:47.820 WARN ADAS [RADAR] sensor=FRONT objects=6 dist=26.3m speed=-0.1km/h status=ALERT",
        "2026-10-05 02:01:47.920 INFO BMS [BATTERY] pack=HV01 voltage=386.20V min_cell=3.094V max_cell=4.108V",
    ]

    print("\n--- Streaming Log Compression ---")
    for log in sample_logs:
        # Send raw log line
        proc.stdin.write(log + "\n")
        proc.stdin.flush()

        # Read JSON response from engine
        response_line = proc.stdout.readline()
        if not response_line:
            break
        
        data = json.loads(response_line)
        if data.get("status") == "OK":
            print(f"Seq #{data['seq']:02d} | Raw: {data['raw_bytes']:3d} B -> Comp: {data['comp_bytes']:2d} B | Ratio: {data['ratio']:5.2f}x | Saved: {data['savings']:5.1f}%")
        else:
            print(f"Engine Message: {data}")

    # Terminate engine cleanly
    proc.stdin.write("QUIT\n")
    proc.stdin.flush()
    proc.communicate()
    print("\n[*] Compression stream completed cleanly.")

if __name__ == "__main__":
    main()
