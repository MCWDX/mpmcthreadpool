cmake_minimum_required(VERSION 3.20)

project(MPMCThreadPoolTest)

set(CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

option(BUILD_TESTS "Build tests" OFF)

if (NOT BUILD_TESTS)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)
    find_package(fmt CONFIG REQUIRED)

    add_library(thread_pool_lib
        ./test/unit/thread_pool_config_test.cpp
        ./src/thread_pool_config.cpp
        ./src/thread_pool.cpp
    )

    target_link_libraries(thread_pool_lib
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        fmt::fmt
    )

    target_include_directories(thread_pool_lib PUBLIC ./include)
else()
    # test文件夹中的 CMakeLists 自行寻找依赖
    add_subdirectory(test)
endif()