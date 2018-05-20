cmake_minimum_required(VERSION 3.6)
project(asynclog)

set(CMAKE_CXX_STANDARD 98)

# complie flags
set(ASYNCLOG_CXXFLAGS "-O2 -fno-omit-frame-pointer" CACHE STRING "additional cxxflags")
set(ASYNCLOG_LDFLAGS "-static-libgcc -static-libstdc++ -static" CACHE SRING "additional ldflags")

add_definitions(-Wall -Wextra -g)
add_definitions(${ASYNCLOG_CXXFLAGS})

# include dir
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# libs
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS thread)

set(LIBS
    rt ${Boost_LIBRARIES}
    ${ASYNCLOG_LDFLAGS}
)

# sample
add_executable(sample
    tests/sample.cpp
)
target_link_libraries(sample ${LIBS})

# benchmark
add_executable(bench_async_log
    tests/bench_async_log.cpp
    tests/stb_sprintf.c
)
target_link_libraries(bench_async_log ${LIBS})
set_target_properties(bench_async_log
    PROPERTIES COMPILE_FLAGS "-DTZ_ASYNCLOG_USE_STB_SPRINTF"
)
target_include_directories(bench_async_log
    PRIVATE tests
)

add_executable(bench_syslog
    tests/bench_syslog.cpp
)

# syslog hook
add_library(asynclog_syslog_hook SHARED
    src/syslog_hook.cpp
)
