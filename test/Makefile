CC = gcc
CFLAGS ?= -O3 -Wall -Wextra -I../include -I../src -std=c99 -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wno-unused-parameter
LDFLAGS ?= -lm -lws2_32 -lpsapi

SRC_FILES = ../src/lds.c ../src/config.c ../src/utils.c ../src/wire.c ../src/model.c ../src/miner.c ../src/encoder.c ../src/decoder.c

all: dirs bin/lds_test_single.exe bin/lds_test_batch.exe

dirs:
	@mkdir -p bin results 2>/dev/null || true

bin/lds_test_single.exe: src/lds_test_single.c $(SRC_FILES)
	$(CC) $(CFLAGS) src/lds_test_single.c $(SRC_FILES) -o $@ $(LDFLAGS)

bin/lds_test_batch.exe: src/lds_test_batch.c $(SRC_FILES)
	$(CC) $(CFLAGS) src/lds_test_batch.c $(SRC_FILES) -o $@ $(LDFLAGS)

clean:
	rm -rf bin/*.exe results/*.txt

.PHONY: all dirs clean
