# 先定义函数再执行脚本

# 检查系统变量中是否存在$VCPKG_ROOT
check_vcpkg_exist() {
    if [ -z "$VCPKG_ROOT" ]; then
        echo "系统变量VCPKG_ROOT未定义，或者为空，无法启用构建脚本。"
        echo "请输入回车回到退出脚本"
        read dummy
        exit 1
    fi
    if [ ! -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]; then
        echo "系统变量VCPKG_ROOT已定义，但${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake文件，无法启用构建脚本。"
        echo "请输入回车回到退出脚本"
        read dummy
        exit 1
    fi
}

# 构建静态库的函数
build_static_library() {
    mkdir -p build
    cd build
    cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DBUILD_TESTS=OFF
    cmake --build ./ -j4
    cd ..
}

# 构建可运行测试文件
build_test_executable() {
    mkdir -p build
    cd build
    cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DBUILD_TESTS=ON
    cmake --build ./ -j4
    cd ..
}

# 检查所需的可运行文件是否存在，不存在则CMake构建
check_test_file_exists() {
    if [ ! -f "$1" ]; then
        build_test_executable
    fi
}

# 进行多生产者多消费者队列功能测试
run_queue_test() {
    check_test_file_exists "./build/test/circular_queue_test"
    cd build/test
    ./circular_queue_test
    cd ../..
}

# 进行线程池配置功能测试
run_config_test() {
    check_test_file_exists "./build/test/thread_pool_config_test"
    cd build/test
    ./thread_pool_config_test
    cd ../..
}

# 进行线程池功能测试
run_thread_pool_test() {
    check_test_file_exists "./build/test/thread_pool_test"
    cd build/test
    ./thread_pool_test --gtest_filter=*:-PerformanceBenchmark.*
    cd ../..
}

# 测试线程池基准性能
run_benchmark() {
    check_test_file_exists "./build/test/thread_pool_test"
    cd build/test
    ./thread_pool_test --gtest_filter=PerformanceBenchmark.*
    cd ../..
}

# 脚本真正开始运行的部分
readonly PROJECT_NAME="MPMCThreadPool"
readonly VERSION="1.0"

# 先检查$VCPKG_ROOT是否存在
check_vcpkg_exist

# 清屏后，保存当前路径位置，并进入脚本文件路径
clear
CUR_PATH=$(pwd)
PROJECT_PATH=$(cd "$(dirname "$0")/.." && pwd)

while true; do
    cd ${PROJECT_PATH}
    echo "----------欢迎使用${PROJECT_NAME}项目部署脚本----------"
    echo "     目前本项目(v${VERSION})支持以下功能:"
    echo "       1:构建多生产者多消费者线程池静态库文件"
    echo "       2:测试多生产者多消费者队列功能"
    echo "       3:测试线程池配置读取功能"
    echo "       4:测试线程池功能"
    echo "       5:进行基准测试"
    echo "       6:清理所有生成文件(删除./build文件夹)"
    echo "       7:清理所有测试文件(删除./build/test文件夹)"
    echo "     其他:退出部署脚本"
    echo "------------------------------------------------------"
    echo -n "你要执行的操作是:"
    read opt
    case "$opt" in 
        "1")
            build_static_library
            ;;
        "2")
            # 测试多生产者多消费者队列
            run_queue_test
            ;;
        "3")
            # 测试线程池配置功能
            run_config_test
            ;;
        "4")
            # 测试线程池功能
            run_thread_pool_test
            ;;
        "5")
            # 测试线程池基准性能
            run_benchmark
            ;;
        "6")
            # 清除所有生成文件
            rm -rf ./build
            ;;
        "7")
            # 清除所有测试文件
            rm -rf ./build/test
            ;;
        *)
            # 退出脚本
            clear
            cd ${CUR_PATH}
            break
            ;;
    esac
    echo "请输入回车回到主菜单"
    read dummy
    clear
done