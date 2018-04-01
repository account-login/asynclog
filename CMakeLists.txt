cmake_minimum_required(VERSION 3.9)
project(asynclog)

set(CMAKE_CXX_STANDARD 98)

add_definitions(-Wall -Wextra -g)
add_definitions(-O2)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS thread)

set(LIBS
    rt ${Boost_LIBRARIES}
    -static-libgcc -static-libstdc++ -static
)

add_executable(sample
    tests/sample.cpp
)
target_link_libraries(sample ${LIBS})

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
