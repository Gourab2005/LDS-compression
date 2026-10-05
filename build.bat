@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo          LDS Native Engine Build System (Windows)
echo =======================================================
echo.

where gcc >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] GCC compiler was not found in PATH!
    echo Please install MinGW GCC or ensure gcc.exe is in your system PATH.
    exit /b 1
)

if not exist build mkdir build
if not exist bin mkdir bin

set CFLAGS=-O3 -Wall -Wextra -Iinclude -Isrc -std=c99 -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wno-unused-parameter
set LDFLAGS=-lm -lws2_32 -lpsapi

echo [1/8] Compiling LDS core modules...
gcc %CFLAGS% -c src/lds.c -o build/lds.o
gcc %CFLAGS% -c src/config.c -o build/config.o
gcc %CFLAGS% -c src/utils.c -o build/utils.o
gcc %CFLAGS% -c src/wire.c -o build/wire.o
gcc %CFLAGS% -c src/model.c -o build/model.o
gcc %CFLAGS% -c src/miner.c -o build/miner.o
gcc %CFLAGS% -c src/encoder.c -o build/encoder.o
gcc %CFLAGS% -c src/decoder.c -o build/decoder.o

echo [2/8] Creating static library build/liblds.a...
ar rcs build/liblds.a build/lds.o build/config.o build/utils.o build/wire.o build/model.o build/miner.o build/encoder.o build/decoder.o

echo [3/8] Building bin/01_single_line_stream.exe...
gcc %CFLAGS% examples/01_single_line_stream.c build/liblds.a -o bin/01_single_line_stream.exe %LDFLAGS%

echo [4/8] Building bin/02_batch_stream.exe...
gcc %CFLAGS% examples/02_batch_stream.c build/liblds.a -o bin/02_batch_stream.exe %LDFLAGS%

echo [5/8] Building bin/03_tcp_sender.exe...
gcc %CFLAGS% examples/03_tcp_sender.c build/liblds.a -o bin/03_tcp_sender.exe %LDFLAGS%

echo [6/8] Building bin/04_tcp_receiver.exe...
gcc %CFLAGS% examples/04_tcp_receiver.c build/liblds.a -o bin/04_tcp_receiver.exe %LDFLAGS%

echo [7/8] Building bin/lds_benchmark.exe...
gcc %CFLAGS% tools/benchmark.c build/liblds.a -o bin/lds_benchmark.exe %LDFLAGS%

echo [8/8] Building stdio streaming engines (encoder/decoder)...
gcc %CFLAGS% tools/lds_encoder_engine.c build/liblds.a -o bin/lds_encoder_engine.exe %LDFLAGS%
gcc %CFLAGS% tools/lds_decoder_engine.c build/liblds.a -o bin/lds_decoder_engine.exe %LDFLAGS%

echo.
echo =======================================================
echo          LDS Build Completed Successfully!
echo =======================================================
echo Executables available in: ./bin/
dir bin\*.exe /b
