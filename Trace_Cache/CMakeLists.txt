cmake_minimum_required(VERSION 2.8)

if ("${CMAKE_VERSION}" VERSION_GREATER "3.0.0")
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function")
endif ()


project(P2)

set(UARCHSIM_BASE ${CMAKE_CURRENT_LIST_DIR}/uarchsim)
set(RVBASE ${CMAKE_CURRENT_LIST_DIR}/riscv-base)

set(SOURCE_FILES
        ${UARCHSIM_BASE}/alu.cc
        ${UARCHSIM_BASE}/BitmapUtils.cc
        ${UARCHSIM_BASE}/bpred_interface.cc
        ${UARCHSIM_BASE}/CacheClass.cc
        ${UARCHSIM_BASE}/checker.cc
        ${UARCHSIM_BASE}/checkpoint.cc
        ${UARCHSIM_BASE}/debug.cc
        ${UARCHSIM_BASE}/decode.cc
        ${UARCHSIM_BASE}/dispatch.cc
        ${UARCHSIM_BASE}/execute.cc
        ${UARCHSIM_BASE}/fetch.cc
        ${UARCHSIM_BASE}/fetch_queue.cc
        ${UARCHSIM_BASE}/histogram.cc
        ${UARCHSIM_BASE}/issue_queue.cc
        ${UARCHSIM_BASE}/lane.cc
        ${UARCHSIM_BASE}/lsu.cc
        ${UARCHSIM_BASE}/main.cc
        ${UARCHSIM_BASE}/parameters.cc
        ${UARCHSIM_BASE}/payload.cc
        ${UARCHSIM_BASE}/pipeline.cc
        ${UARCHSIM_BASE}/pipeline_register.cc
        ${UARCHSIM_BASE}/register_read.cc
        ${UARCHSIM_BASE}/rename.cc
        ${UARCHSIM_BASE}/renamer.cc
        ${UARCHSIM_BASE}/retire.cc
        ${UARCHSIM_BASE}/schedule.cc
        ${UARCHSIM_BASE}/sim.cc
        ${UARCHSIM_BASE}/squash.cc
        ${UARCHSIM_BASE}/stats.cc
        ${UARCHSIM_BASE}/writeback.cc
        ${UARCHSIM_BASE}/trace_cache.cc
        )

set(HEADER_FILES
        ${UARCHSIM_BASE}/BitmapUtils.h
        ${UARCHSIM_BASE}/bpred_interface.h
        ${UARCHSIM_BASE}/CacheClass.h
        ${UARCHSIM_BASE}/cache.h
        ${UARCHSIM_BASE}/CircularQueue.h
        ${UARCHSIM_BASE}/DataDefine.h
        ${UARCHSIM_BASE}/debug.h
        ${UARCHSIM_BASE}/fetch_queue.h
        ${UARCHSIM_BASE}/fu.h
        ${UARCHSIM_BASE}/histogram.h
        ${UARCHSIM_BASE}/issue_queue.h
        ${UARCHSIM_BASE}/lane.h
        ${UARCHSIM_BASE}/lsu.h
        ${UARCHSIM_BASE}/parameters.h
        ${UARCHSIM_BASE}/payload.h
        ${UARCHSIM_BASE}/pipeline.h
        ${UARCHSIM_BASE}/pipeline_register.h
        ${UARCHSIM_BASE}/renamer.h
        ${UARCHSIM_BASE}/sim.h
        ${UARCHSIM_BASE}/stats.h
        ${UARCHSIM_BASE}/svdpi.h
        ${UARCHSIM_BASE}/trace_cache.h)

#add_compile_definitions(RISCV_MICRO_CHECKER)

#add_compile_definitions(PREFIX=".")

include_directories(
        ${UARCHSIM_BASE}
        ${RVBASE}
        ${RVBASE}/softfloat
        ${RVBASE}/spike_main
)

link_directories(
        ${RVBASE}
)

add_executable(
        721sim
        ${SOURCE_FILES}
        ${HEADER_FILES}
)

target_link_libraries(
        721sim
        riscv-base
        pthread
        z
        dl
)

target_compile_definitions(
        721sim
        PRIVATE
        RISCV_MICRO_CHECKER
        PREFIX="."
)
