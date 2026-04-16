cmake_minimum_required(VERSION 3.20)

project(MPMCThreadPoolTest)

set(CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/../include)

add_library(lib INTERFACE)

target_include_directories(lib INTERFACE
    ${PROJECT_SOURCE_DIR}/../include
)

add_executable(circular_queue_test ./unit/circular_queue_test.cpp)
target_link_libraries(circular_queue_test PRIVATE 
    lib
    GTest::gtest_main
)

add_executable(thread_pool_config_test 
    ./unit/thread_pool_config_test.cpp
    ./../src/thread_pool_config.cpp
    ./../src/thread_pool.cpp
    ./../src/logger.cpp
)
target_link_libraries(thread_pool_config_test PRIVATE
    lib
    GTest::gtest_main
    spdlog::spdlog
    fmt::fmt
)

add_executable(thread_pool_test 
    ./unit/thread_pool_test.cpp
    ./../src/thread_pool_config.cpp
    ./../src/thread_pool.cpp
    ./../src/logger.cpp
)
target_link_libraries(thread_pool_test PRIVATE
    lib
    GTest::gtest_main
    spdlog::spdlog
    fmt::fmt
)
