@echo off
setlocal enabledelayedexpansion

echo =======================================================================
echo          LDS Benchmark & Speed Test Suite (With Timers)
echo =======================================================================
echo.
echo Select Test Mode:
echo.
echo   [1] Run BOTH (Single-Line + Batch-Wise) on FULL Datasets (All Lines)
echo   [2] Run BOTH (Single-Line + Batch-Wise) Fast Sample (50,000 lines each)
echo   [3] Run Single-Line Streaming Only (FULL Datasets)
echo   [4] Run Single-Line Streaming Only (Fast Sample - 50,000 lines)
echo   [5] Run Batch-Wise Streaming Only  (FULL Datasets - 16 KB)
echo   [6] Run Batch-Wise Streaming Only  (Fast Sample - 16 KB)
echo   [7] Quick Test on Local Sample File (data/logs.txt)
echo   [8] Rebuild C Benchmark Engines
echo.

set /p choice="Enter option (1-8, default 1): "
if "%choice%"=="" set choice=1

if "%choice%"=="1" (
    echo.
    echo ==============================================================
    echo [*] Step 1/2: Running Single-Line Streaming (FULL DATASETS)...
    echo ==============================================================
    node run_single_test.js --full
    echo.
    echo ==============================================================
    echo [*] Step 2/2: Running Batch-Wise Streaming (FULL DATASETS)...
    echo ==============================================================
    node run_batch_test.js --full
    goto done
)

if "%choice%"=="2" (
    echo.
    echo ==============================================================
    echo [*] Step 1/2: Running Single-Line Streaming (Fast Sample)...
    echo ==============================================================
    node run_single_test.js
    echo.
    echo ==============================================================
    echo [*] Step 2/2: Running Batch-Wise Streaming (Fast Sample)...
    echo ==============================================================
    node run_batch_test.js
    goto done
)

if "%choice%"=="3" (
    echo.
    echo [*] Running Single-Line Streaming Benchmark (FULL DATASETS)...
    node run_single_test.js --full
    goto done
)

if "%choice%"=="4" (
    echo.
    echo [*] Running Single-Line Streaming Benchmark (Fast Sample)...
    node run_single_test.js
    goto done
)

if "%choice%"=="5" (
    echo.
    echo [*] Running Batch-Wise Streaming Benchmark (FULL DATASETS - 16 KB)...
    node run_batch_test.js --full
    goto done
)

if "%choice%"=="6" (
    echo.
    echo [*] Running Batch-Wise Streaming Benchmark (Fast Sample - 16 KB)...
    node run_batch_test.js
    goto done
)

if "%choice%"=="7" (
    echo.
    echo [*] Running Quick Single-Line Test on data/logs.txt...
    bin\lds_test_single.exe "..\data\logs.txt"
    echo.
    echo [*] Running Quick Batch-Wise Test on data/logs.txt (16 KB)...
    bin\lds_test_batch.exe "..\data\logs.txt" 16384
    goto done
)

if "%choice%"=="8" (
    echo.
    echo [*] Rebuilding test benchmark engines...
    gcc -O3 -Wall -Wextra -I../include -I../src -std=c99 -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wno-unused-parameter src/lds_test_single.c ../src/lds.c ../src/config.c ../src/utils.c ../src/wire.c ../src/model.c ../src/miner.c ../src/encoder.c ../src/decoder.c -o bin/lds_test_single.exe -lm -lws2_32 -lpsapi
    gcc -O3 -Wall -Wextra -I../include -I../src -std=c99 -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wno-unused-parameter src/lds_test_batch.c ../src/lds.c ../src/config.c ../src/utils.c ../src/wire.c ../src/model.c ../src/miner.c ../src/encoder.c ../src/decoder.c -o bin/lds_test_batch.exe -lm -lws2_32 -lpsapi
    echo [*] Rebuild completed successfully.
    goto done
)

:done
echo.
echo ==============================================================
echo [*] All requested benchmarks finished!
echo [*] Reports saved to:
echo     - results\benchmark_results_single.txt
echo     - results\benchmark_results_batch.txt
echo ==============================================================
echo.
pause
