########################################################################
########################################################################

cmake_minimum_required(VERSION 3.20.3)
project(Frost-Tools VERSION 1.0)

########################################################################
########################################################################

if (CMAKE_BUILD_TYPE MATCHES "Release")
    set(COMPILE_OPTIONS
        -Ofast
        -fstrict-aliasing
        -march=native
        -Wno-deprecated
        -Wno-unknown-warming-option
        -Wno-self-assign
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wunused)
elseif (CMAKE_BUILD_TYPE MATCHES "Profile")
    set(COMPILE_OPTIONS
        -fstrict-aliasing
        -Wno-deprecated
        -Wno-unknown-warming-option
        -Wno-self-assign
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wunused
        -pg -g)
else()
    set(COMPILE_OPTIONS
        -g
        -Wall)
endif()

set(CMAKE_CXX_STANDARD 17)

########################################################################
########################################################################

find_package(ZLIB)
find_package(LibLZMA)

########################################################################
########################################################################
### CACHESIM ###

add_executable(cachesim cachesim.cpp
    # from Frost src
    ../src/util/argparse.cpp
    # From tools src
    )
target_compile_options(cachesim PRIVATE ${COMPILE_OPTIONS})
target_include_directories(cachesim PRIVATE "src" "../src")
target_link_libraries(cachesim PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)

if (NOT CACHESIM_SIZE_MB)
    set(CACHESIM_SIZE_MB 2)
endif()

if (NOT CACHESIM_ASSOC)
    set(CACHESIM_ASSOC 16)
endif()

if (NOT CACHESIM_REPL)
    set(CACHESIM_REPL LRU)
endif()

target_compile_definitions(cachesim PRIVATE CACHE_SIZE_MB=${CACHESIM_SIZE_MB}
                                            CACHE_ASSOC=${CACHESIM_ASSOC}
                                            CACHE_REPL=CacheReplPolicy::${CACHESIM_REPL})

########################################################################
########################################################################
### PORT_CHAMPSIM_TO_MTF ###

add_executable(port_from_champsim port_from_champsim.cpp)
target_compile_options(port_from_champsim PRIVATE ${COMPILE_OPTIONS})
target_include_directories(port_from_champsim PRIVATE "src" "../src")
target_link_libraries(port_from_champsim PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)

if (PORT_CTF_OUTPUT_FORMAT)
    target_compile_definitions(port_from_champsim PRIVATE TRACE_FORMAT_${PORT_CTF_OUTPUT_FORMAT})
endif()

########################################################################
########################################################################

