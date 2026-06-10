cmake_minimum_required(VERSION 3.20)

# Enable OBJCXX so CMake compiles .mm files as Objective-C++.
if(APPLE)
    project(fec C CXX OBJCXX)
else()
    project(fec C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

set(FEC_SOURCES
    src/interleaver.cpp
    src/deinterleaver.cpp
    src/pipeline.cpp
    src/rs_encode.cpp
    src/rs_decode.cpp
)

set(FEC_C_SOURCES
    src/rs_decode_c.c
)

if(APPLE)
    list(APPEND FEC_SOURCES src/interleaver_metal.mm)
    list(APPEND FEC_SOURCES src/rs_encode_metal.mm)
endif()

add_library(fec_rs_decode_c ${FEC_C_SOURCES})
target_include_directories(fec_rs_decode_c PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(fec_rs_decode_c PRIVATE -O3)

add_library(fec_lib ${FEC_SOURCES})
target_include_directories(fec_lib PUBLIC include)
target_include_directories(fec_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(fec_lib PRIVATE -Wall -Wextra -O3)
target_link_libraries(fec_lib PRIVATE fec_rs_decode_c)

find_package(Threads REQUIRED)
target_link_libraries(fec_lib PUBLIC Threads::Threads)

if(APPLE)
    target_link_libraries(fec_lib PUBLIC
        "-framework Metal"
        "-framework Foundation"
    )
endif()

add_executable(fec_test test/main.cpp)
target_link_libraries(fec_test PRIVATE fec_lib)
target_compile_options(fec_test PRIVATE -Wall -Wextra -O3)

add_executable(stream_bench test/stream_bench.cpp)
target_link_libraries(stream_bench PRIVATE fec_lib)
target_compile_options(stream_bench PRIVATE -Wall -Wextra -O3)
