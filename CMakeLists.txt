cmake_minimum_required(VERSION 3.16)
project(newlibbroker)

# Release Debug
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall")
add_compile_options(-std=c++17 -Wall -O2)

SET(ROOT_DIR "/home/work/sys/lib")
macro(SUBDIRLIST result curdir)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    set(dirlist "")
    foreach(child ${children})
        if(IS_DIRECTORY ${curdir}/${child})
            LIST(APPEND dirlist ${child})
        endif()
    endforeach()
    set(${result} ${dirlist})
endmacro()

SUBDIRLIST(all_sub_index ${ROOT_DIR})
foreach(child ${all_sub_index})
    list(APPEND all_include ${ROOT_DIR}/${child}/include)
    list(APPEND all_lib ${ROOT_DIR}/${child}/lib)
endforeach()

# Aeron C++ 配置
set(AERON_SOURCE_DIR "/home/work/sys/kaiyuan/aeron-master")
set(AERON_BUILD_DIR "/home/work/sys/kaiyuan/aeron-master/cppbuild/Release")

set(AERON_C_CLIENT_INCLUDE_DIR "${AERON_SOURCE_DIR}/aeron-client/src/main/c")
set(AERON_CPP_WRAPPER_INCLUDE_DIR "${AERON_SOURCE_DIR}/aeron-client/src/main/cpp_wrapper")
set(AERON_LIB_DIR "${AERON_BUILD_DIR}/lib")

include_directories(${AERON_C_CLIENT_INCLUDE_DIR} ${AERON_CPP_WRAPPER_INCLUDE_DIR} ${all_include})
link_directories(${AERON_LIB_DIR} ${all_lib})

aux_source_directory (./src/mem_broker LIB_LIST)
add_library (membroker ${LIB_LIST})

aux_source_directory (./src/imitate_broker MEM_TEST_LIST)
add_executable(imitate_broker ${MEM_TEST_LIST})
target_link_libraries(imitate_broker
        membroker aeron coral x stdc++fs yaml-cpp boost_date_time boost_filesystem boost_regex boost_system  boost_chrono boost_log boost_program_options boost_thread boost_iostreams boost_context boost_coroutine z protobuf protobuf-lite sodium ssl crypto iconv pthread dl)

add_executable(send_req ./src/send_req/send_req.cc)
target_link_libraries(send_req
        membroker aeron coral x stdc++fs yaml-cpp boost_date_time boost_filesystem boost_regex boost_system  boost_chrono boost_log boost_program_options boost_thread boost_iostreams boost_context boost_coroutine z protobuf protobuf-lite sodium ssl crypto iconv pthread dl)


aux_source_directory (./src/crtp SRC_CRTP)
add_executable(crtp src/crtp/main.cc src/crtp/mem_base_broker.cc src/crtp/imitate_broker.cc)
target_link_libraries(crtp coral x stdc++fs yaml-cpp boost_date_time boost_filesystem boost_regex boost_system  boost_chrono boost_log boost_program_options boost_thread boost_iostreams z protobuf protobuf-lite sodium ssl crypto iconv pthread dl)


add_executable(test_flow_control ./src/gtest/test_flow_control.cc)
target_link_libraries(test_flow_control membroker gtest gtest_main coral x stdc++fs yaml-cpp boost_date_time boost_filesystem boost_regex boost_system  boost_chrono boost_log boost_program_options boost_thread boost_iostreams boost_context boost_coroutine z protobuf protobuf-lite sodium ssl crypto iconv pthread dl)
