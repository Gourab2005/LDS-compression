CC = gcc
CFLAGS ?= -O3 -Wall -Wextra -Iinclude -Isrc -std=c99 -D_GNU_SOURCE -D_DEFAULT_SOURCE -Wno-unused-parameter
LDFLAGS ?= -lm

ifeq ($(OS),Windows_NT)
    WS2_LIB = -lws2_32 -lpsapi
    LIB_EXT = .dll
    EXE_EXT = .exe
else
    WS2_LIB =
    LIB_EXT = .so
    EXE_EXT =
endif

SRC_FILES = src/lds.c src/config.c src/utils.c src/wire.c src/model.c src/miner.c src/encoder.c src/decoder.c
OBJ_FILES = $(patsubst src/%.c, build/%.o, $(SRC_FILES))

STATIC_LIB = build/liblds.a
SHARED_LIB = build/liblds$(LIB_EXT)

BINARIES = \
    bin/01_single_line_stream$(EXE_EXT) \
    bin/02_batch_stream$(EXE_EXT) \
    bin/03_tcp_sender$(EXE_EXT) \
    bin/04_tcp_receiver$(EXE_EXT) \
    bin/lds_benchmark$(EXE_EXT) \
    bin/lds_encoder_engine$(EXE_EXT) \
    bin/lds_decoder_engine$(EXE_EXT)

all: dirs $(STATIC_LIB) $(SHARED_LIB) $(BINARIES)

dirs:
	@mkdir -p build bin 2>/dev/null || true

build/%.o: src/%.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(STATIC_LIB): $(OBJ_FILES)
	ar rcs $@ $(OBJ_FILES)

$(SHARED_LIB): $(OBJ_FILES)
	$(CC) -shared -o $@ $(OBJ_FILES) $(LDFLAGS)

bin/01_single_line_stream$(EXE_EXT): examples/01_single_line_stream.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS)

bin/02_batch_stream$(EXE_EXT): examples/02_batch_stream.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS)

bin/03_tcp_sender$(EXE_EXT): examples/03_tcp_sender.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS) $(WS2_LIB)

bin/04_tcp_receiver$(EXE_EXT): examples/04_tcp_receiver.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS) $(WS2_LIB)

bin/lds_benchmark$(EXE_EXT): tools/benchmark.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS) $(WS2_LIB)

bin/lds_encoder_engine$(EXE_EXT): tools/lds_encoder_engine.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS) $(WS2_LIB)

bin/lds_decoder_engine$(EXE_EXT): tools/lds_decoder_engine.c $(STATIC_LIB)
	$(CC) $(CFLAGS) $< $(STATIC_LIB) -o $@ $(LDFLAGS) $(WS2_LIB)

clean:
	rm -rf build bin/*.exe bin/*.so bin/*.dll bin/01_* bin/02_* bin/03_* bin/04_* bin/lds_*

.PHONY: all dirs clean
