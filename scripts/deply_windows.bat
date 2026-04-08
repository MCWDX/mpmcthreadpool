@echo off
setlocal enabledelayedexpansion

:: 先检查是否有VCPKG_ROOT，以及是否有效
chcp 65001 > nul
call :check_vcpkg_exist || exit /b 1

set "COMPILER_SET=MSVC G++"
echo "进入构建脚本前请选择所用编译器, 目前支持编译器有%COMPILER_SET%"
set /p "COMPILER=你的编译器是(无输入默认使用MSVC): "

if "!COMPILER!"=="" (
    set "COMPILER=MSVC"
) else (
    set "found=0"
    for %%i in (%COMPILER_SET%) do (
        if /i "!COMPILER!"=="%%i" (
            set "found=1"
            set "COMPILER=%%i"
        )
    )
    if !found! neq 1 (
    echo "编译器%~1未支持，目前仅支持%COMPILER_SET%"
    exit /b 1
    )
)

if "!COMPILER!"=="G++" (
    call :check_mingw_exist || exit /b 1
)

cls
set "CUR_PATH=%CD%"
set "PROJECT_NAME=MPMCThreadPool"
set "VERSION=1.0"

cd /d "%~dp0.."

:loop
    echo "----------欢迎使用%PROJECT_NAME%项目部署脚本----------"
    echo "     目前本项目(v%VERSION%)支持以下功能:"
    echo "       1: 构建多生产者多消费者线程池静态库文件"
    echo "       2: 测试多生产者多消费者队列功能"
    echo "       3: 测试线程池配置读取功能"
    echo "       4: 测试线程池功能"
    echo "       5: 进行基准测试"
    echo "       6:清理所有生成文件(删除./build文件夹)"
    echo "       7:清理所有测试文件(删除./build/test文件夹)"
    echo "     其他: 退出部署脚本"
    echo "     当前环境所用编译器为: %COMPILER%"
    echo "------------------------------------------------------"
    set "opt="
    set /p "opt=你要执行的操作是:"
    echo !opt!|findstr /r "^[0-9][0-9]*$" >nul
    if errorlevel 1 (
        goto end_of_bat
    )
    2>nul call :opt_!opt! || goto end_of_bat
    pause
    cls
    goto loop

:end_of_bat 
:: 批处理结束位点
cls
cd %CUR_PATH%
exit /b 0

:check_vcpkg_exist
    :: 检查系统变量中是否存在有效的VCPKG_ROOT变量
    if not defined VCPKG_ROOT (
        echo "系统变量VCPKG_ROOT未定义，或者为空，无法启用构建脚本。"
        pause
        exit /b 1
    ) else if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        echo "系统变量VCPKG_ROOT已定义，但%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake文件，无法启用构建脚本。"
        pause
        exit /b 1
    )
    goto :eof

:check_mingw_exist
    :: 检查系统变量中是否存在有效的MINGW_ROOT变量
    if not defined MINGW_ROOT (
        echo "系统变量MINGW_ROOT未定义，或者为空，无法使用G++编译器。"
        pause
        exit /b 1
    ) else if not exist "%MINGW_ROOT%\bin\g++.exe" (
        echo "系统变量MINGW_ROOT已定义，但%MINGW_ROOT%\bin\g++.exe文件，请检查路径。"
        pause
        exit /b 1
    ) else if not exist "%MINGW_ROOT%\bin\gcc.exe" (
        echo "系统变量MINGW_ROOT已定义，但%MINGW_ROOT%\bin\gcc.exe文件，请检查路径。"
        pause
        exit /b 1
    )
    goto :eof

:opt_1
    :: 构建MPMCThreadPool静态库
    if not exist .\build mkdir .\build
    cd .\build
    call :load_cmake OFF
    cmake --build .\ -j4
    cd ..
    goto :eof

:opt_2
    :: 进行多生产者多消费者队列功能测试
    call :build_test_executable ".\build\test\circular_queue_test"
    cd .\build\test
    .\circular_queue_test.exe
    cd ..\..
    goto :eof

:opt_3
    :: 进行线程池配置功能测试
    call :build_test_executable ".\build\test\thread_pool_config_test"
    cd .\build\test
    .\thread_pool_config_test.exe
    cd ..\..
    goto :eof

:opt_4
    :: 进行线程池功能测试
    call :build_test_executable ".\build\test\thread_pool_test"
    cd .\build\test
    .\thread_pool_test --gtest_filter=*:-PerformanceBenchmark.*
    cd ..\..
    goto :eof

:opt_5
    :: 测试线程池基准性能
    call :build_test_executable ".\build\test\thread_pool_test"
    cd .\build\test
    .\thread_pool_test --gtest_filter=PerformanceBenchmark.*
    cd ..\..
    goto :eof

:opt_6
    :: 删除所有生成文件
    rmdir /s /q .\build 2>nul
    goto :eof

:opt_7
    :: 删除所有测试文件
    rmdir /s /q .\build\test 2>nul
    goto :eof

:build_test_executable
    :: 构建可运行测试文件
    if exist %~1 (
        :: 文件存在，不进行重复构建
        goto :eof
    )
    if not exist .\build mkdir .\build
    cd .\build
    call :load_cmake ON
    cmake --build .\ -j4
    cd ..
    goto :eof

:load_cmake
    :: 加载cmake
    if "!COMPILER!"=="MSVC" (
        cmake .. "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" "-DBUILD_TESTS=%~1"
    ) else if "!COMPILER!"=="G++" (
        cmake .. -G "MinGW Makefiles" ^
        "-DCMAKE_CXX_COMPILER=%MINGW_ROOT%\bin\g++.exe" ^
        "-DCMAKE_C_COMPILER=%MINGW_ROOT%\bin\gcc.exe" ^
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-static" ^
        "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
        "-DBUILD_TESTS=%~1"
    )

    goto :eof
