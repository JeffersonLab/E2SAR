cmake_minimum_required(VERSION 3.5.1)
project(E2SAR_JAVA CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release MinSizeRel RelWithDebInfo)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(JNI REQUIRED)
find_package(Boost COMPONENTS system url thread chrono random program_options REQUIRED )
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_program(PKG_CONFIG_EXECUTABLE pkg-config)
find_package(Protobuf REQUIRED)

pkg_check_modules(E2SAR REQUIRED e2sar)
pkg_check_modules(GRPC REQUIRED grpc++)

if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
endif()

include_directories(${E2SAR_INCLUDE_DIRS} ${JNI_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIR} include)
message(STATUS "E2SAR_INCLUDE_DIRS: ${E2SAR_INCLUDE_DIRS}")
message(STATUS "E2SAR linker flags: ${E2SAR_LIBRARIES}")
message(STATUS "GRPC linker flags: ${GRPC_LIBRARIES}")
message(STATUS "Protobuf linker flags: ${Protobuf_LIBRARIES}")

add_library(jnie2sar SHARED src/cpp/jnie2sar.cpp src/cpp/jniLbManager.cpp src/cpp/jnie2sarHelper.cpp src/cpp/jnie2sarEjfatUri.cpp src/cpp/jniDpSegmenter.cpp src/cpp/jniDpReassembler.cpp)
target_link_libraries(jnie2sar ${E2SAR_LIBRARIES}
${Boost_LIBRARIES} ${GRPC_LIBRARIES} ${Protobuf_LIBRARIES} Threads::Threads ${JNI_LIBRARIES})
if (CMAKE_CXX_COMPILER_ID MATCHES Clang)
  target_compile_options(jnie2sar PRIVATE -Wno-return-type-c-linkage)
endif()
install(TARGETS jnie2sar DESTINATION "./") 