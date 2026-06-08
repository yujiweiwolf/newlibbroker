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

include_directories(${all_include} )
link_directories(${all_lib})

find_package(iceoryx2-cxx 0.8.0 REQUIRED)
include_directories(/usr/local/include/iceoryx2/v0.8.0)

aux_source_directory (./src/mem_broker LIB_LIST)
add_library (membroker ${LIB_LIST})

aux_source_directory (./src/imitate_broker MEM_TEST_LIST)
add_executable(imitate_broker ${MEM_TEST_LIST})
target_link_libraries(imitate_broker
        membroker coral x stdc++fs yaml-cpp boost_date_time boost_filesystem boost_regex boost_system  boost_chrono boost_log boost_program_options boost_thread boost_iostreams boost_context boost_coroutine z protobuf protobuf-lite sodium ssl crypto iconv pthread dl iceoryx2-cxx::static-lib-cxx)
