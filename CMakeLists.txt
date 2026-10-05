cmake_minimum_required(VERSION 3.10)
project(lds C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/O2 /W3)
else()
    add_compile_options(-O3 -Wall -Wextra)
endif()

include_directories(include src)

set(LDS_SOURCES
    src/lds.c
    src/config.c
    src/utils.c
    src/wire.c
    src/model.c
    src/miner.c
    src/encoder.c
    src/decoder.c
)

# Static Library
add_library(lds_static STATIC ${LDS_SOURCES})
set_target_properties(lds_static PROPERTIES OUTPUT_NAME "lds")

# Shared Library
add_library(lds SHARED ${LDS_SOURCES})

# OS-specific link libraries
set(EXTRA_LIBS "")
if(WIN32)
    list(APPEND EXTRA_LIBS ws2_32 psapi)
else()
    list(APPEND EXTRA_LIBS m)
endif()

target_link_libraries(lds_static PRIVATE ${EXTRA_LIBS})
target_link_libraries(lds PRIVATE ${EXTRA_LIBS})

# Examples
add_executable(01_single_line_stream examples/01_single_line_stream.c)
target_link_libraries(01_single_line_stream PRIVATE lds_static ${EXTRA_LIBS})

add_executable(02_batch_stream examples/02_batch_stream.c)
target_link_libraries(02_batch_stream PRIVATE lds_static ${EXTRA_LIBS})

add_executable(03_tcp_sender examples/03_tcp_sender.c)
target_link_libraries(03_tcp_sender PRIVATE lds_static ${EXTRA_LIBS})

add_executable(04_tcp_receiver examples/04_tcp_receiver.c)
target_link_libraries(04_tcp_receiver PRIVATE lds_static ${EXTRA_LIBS})

# Tools
add_executable(lds_benchmark tools/benchmark.c)
target_link_libraries(lds_benchmark PRIVATE lds_static ${EXTRA_LIBS})

add_executable(lds_encoder_engine tools/lds_encoder_engine.c)
target_link_libraries(lds_encoder_engine PRIVATE lds_static ${EXTRA_LIBS})

add_executable(lds_decoder_engine tools/lds_decoder_engine.c)
target_link_libraries(lds_decoder_engine PRIVATE lds_static ${EXTRA_LIBS})

# Installation
install(TARGETS lds lds_static
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES include/lds.h DESTINATION include)
