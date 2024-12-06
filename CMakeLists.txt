cmake_minimum_required(VERSION 3.20.3)
project(Frost VERSION 0.1)

if (CMAKE_BUILD_TYPE MATCHES "Release")
    set(COMPILE_OPTIONS
        -Ofast
#       -flto=auto
        -funroll-loops
        -fno-strict-aliasing
        -Wno-deprecated
        -Wno-unknown-warming-option
        -Wno-self-assign
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wunused)
elseif (CMAKE_BUILD_TYPE MATCHES "Profile")
    set(COMPILE_OPTIONS
        -funroll-loops
        -fno-strict-aliasing
        -Wno-deprecated
        -Wno-unknown-warming-option
        -Wno-self-assign
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wunused
        -O1 -g)
else()
    set(COMPILE_OPTIONS
        -g
        -Wall)
endif()

set(CMAKE_CXX_STANDARD 17)

set(SIM_FILES
    src/core.cpp
    src/dram.cpp
    src/io_bus.cpp
    src/os.cpp
    src/dram/channel.cpp
    src/trace/data.cpp
    src/trace/reader.cpp
    src/util/argparse.cpp
    # Generated files
    _generated/${BUILD_ID}/sim.cpp)
find_package(ZLIB)
find_package(LibLZMA)

add_executable(sim src/main.cpp ${SIM_FILES})
target_compile_options(sim PRIVATE ${COMPILE_OPTIONS})
target_include_directories(sim PRIVATE "src" "_generated/${BUILD_ID}")
target_link_libraries(sim PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)
