cmake_minimum_required(VERSION 3.20)

# Enable OBJCXX so CMake compiles .mm files as Objective-C++.
if(APPLE)
    project(fec CXX OBJCXX)
else()
    project(fec CXX)
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
)

if(APPLE)
    list(APPEND FEC_SOURCES src/interleaver_metal.mm)
    list(APPEND FEC_SOURCES src/rs_encode_metal.mm)
endif()

add_library(fec_lib ${FEC_SOURCES})
target_include_directories(fec_lib PUBLIC include)
target_compile_options(fec_lib PRIVATE -Wall -Wextra -O3)

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
