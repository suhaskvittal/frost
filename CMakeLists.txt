########################################################################
########################################################################

cmake_minimum_required(VERSION 3.20.3)
project(Frost VERSION 1.0)

########################################################################
########################################################################

if (CMAKE_BUILD_TYPE MATCHES "Release")
    set(COMPILE_OPTIONS
        -Ofast
        -funroll-loops
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
        -fno-strict-aliasing
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

set(SIMPLE_CORE_MODEL_FILES
    src/simple_model/core.cpp
    src/simple_model/os.cpp)

set(COMPLEX_CORE_MODEL_FILES
    src/complex_model/core.cpp
    src/complex_model/os.cpp
    src/complex_model/os/ptw/cache.cpp
    src/complex_model/os/ptw.cpp
    src/complex_model/os/vmem.cpp)

file(READ _generated/${BUILD_ID}/core_model.txt CORE_MODEL)
file(READ _generated/${BUILD_ID}/defines.txt CUSTOM_DEFINES)

if (CORE_MODEL MATCHES "simple")
    set(CORE_MODEL_FILES ${SIMPLE_CORE_MODEL_FILES})
else()
    set(CORE_MODEL_FILES ${COMPLEX_CORE_MODEL_FILES})
endif()

message("================== CONFIGURATION =======================")
message("build id = ${BUILD_ID}")
message("core model = ${CORE_MODEL}")
message("defines = ${CUSTOM_DEFINES}")
message("========================================================")

########################################################################
########################################################################

set(MAIN_SIM_FILES
    src/dram.cpp
    src/dram/bank.cpp
    src/dram/channel.cpp
    src/dram/command.cpp
    src/instruction.cpp
    src/io_bus.cpp
    src/os/free_list.cpp
    src/transaction.cpp
    src/util/argparse.cpp
    # Generated files
    _generated/${BUILD_ID}/sim.cpp
    # CORE MODEL FILES)
    ${CORE_MODEL_FILES})

########################################################################
########################################################################

find_package(ZLIB)
find_package(LibLZMA)

########################################################################
########################################################################

add_executable(sim src/main.cpp ${MAIN_SIM_FILES})
target_compile_options(sim PRIVATE ${COMPILE_OPTIONS})
target_include_directories(sim PRIVATE "src" "_generated/${BUILD_ID}")
target_link_libraries(sim PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)
target_compile_definitions(sim PRIVATE ${CUSTOM_DEFINES})
if (CMAKE_BUILD_TYPE MATCHES "Profile")
    target_link_libraries(sim PRIVATE -pg)
endif()

########################################################################
########################################################################
