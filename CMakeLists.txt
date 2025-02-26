########################################################################
########################################################################

cmake_minimum_required(VERSION 3.20.3)
project(Frost VERSION 1.0)

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
file(READ _generated/${BUILD_ID}/trace_format.txt TRACE_FORMAT)
file(READ _generated/${BUILD_ID}/defines.txt CUSTOM_DEFINES)

if (CORE_MODEL MATCHES "simple")
    set(CORE_MODEL_FILES ${SIMPLE_CORE_MODEL_FILES})
else()
    set(CORE_MODEL_FILES ${COMPLEX_CORE_MODEL_FILES})
endif()

message("================== CONFIGURATION =======================")
message("build id = ${BUILD_ID}")
message("core model = ${CORE_MODEL}")
message("trace format = ${TRACE_FORMAT}")
message("defines = ${CUSTOM_DEFINES}")
message("========================================================")

########################################################################
########################################################################

set(MAIN_SIM_FILES
    src/cache/ext/set_dueling.cpp
    src/cache/indexing.cpp
    src/os/free_list.cpp
    src/util/argparse.cpp
    src/util/stats/cache.cpp
    # Generated files
    _generated/${BUILD_ID}/sim.cpp
    # CORE MODEL FILES)
    ${CORE_MODEL_FILES})

########################################################################
########################################################################

if (USE_DRAMSIM3)
    message("linking against dramsim3")
    add_subdirectory(DRAMsim3)
    
    set(MAIN_SIM_FILES ${MAIN_SIM_FILES} src/dramsim3_wrapper.cpp)
else()
    set(MAIN_SIM_FILES ${MAIN_SIM_FILES}
                src/dram.cpp
                src/dram/channel.cpp
                src/dram/command.cpp
                src/dram/scheduler.cpp
                src/dram/scheduler/impl.cpp
                src/dram/state.cpp)
endif()

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
target_compile_definitions(sim PRIVATE TRACE_FORMAT_${TRACE_FORMAT} ${CUSTOM_DEFINES})
if (CMAKE_BUILD_TYPE MATCHES "Profile")
    target_link_libraries(sim PRIVATE -pg)
endif()

if (USE_DRAMSIM3)
    target_link_libraries(sim PRIVATE dramsim3)
    target_compile_definitions(sim PRIVATE USE_DRAMSIM3)
endif()

########################################################################
########################################################################
