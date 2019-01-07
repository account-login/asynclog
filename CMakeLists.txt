cmake_minimum_required(VERSION 2.8)
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
    tests/stb_sprintf.c
)
set_target_properties(sample
    PROPERTIES COMPILE_FLAGS "-DTZ_ASYNCLOG_USE_STB_SPRINTF"
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
include_directories(tests)
#target_include_directories(bench_async_log
#    PRIVATE tests
#)

add_executable(bench_syslog
    tests/bench_syslog.cpp
)
target_link_libraries(bench_syslog
    rt ${Boost_LIBRARIES}
)

# syslog hook
add_library(asynclog_syslog_hook SHARED
    src/syslog_hook.cpp tests/stb_sprintf.c
)
set_target_properties(asynclog_syslog_hook
    PROPERTIES COMPILE_FLAGS "-DTZ_ASYNCLOG_USE_STB_SPRINTF"
)

find_package (Threads)
target_link_libraries(asynclog_syslog_hook
    ${CMAKE_THREAD_LIBS_INIT}
    dl
)

add_custom_target(asynclog_syslog_hook_preconfig
    DEPENDS asynclog_syslog_hook
)
add_custom_command(TARGET asynclog_syslog_hook_preconfig
    POST_BUILD
    COMMAND bash -c "cat libasynclog_syslog_hook.so <(echo '<ASYNCLOG>') ../asynclog.json >libasynclog_syslog_hook_preconfig.so"
    COMMENT "generate preconfigured hook"
    VERBATIM
)
