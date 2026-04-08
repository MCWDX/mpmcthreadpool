# MPMCThreadPool

## 项目结构
```
- build(dir)                        # 构建目录，运行构建脚本后会生成，
- CMakeLists.txt                    # CMake 构建配置
- config(dir)                       # 存放配置 JSON 文件的目录
- - config.json                     # 线程池配置文件
- - logger_config.json              # 线程池日志记录器配置文件
- - ...(一些默认配置与建议配置)
- include(dir)                      # 头文件目录
- - circular_queue.h                # 循环队列实现
- - example_tasks.h                 # 任务示例头文件
- - logger.h                        # 日志系统头文件
- - mpmc_blocking_queue.h           # 多生产者多消费者阻塞队列实现
- - thread_pool_config.h            # 线程池配置系统头文件
- - thread_pool_improved.h          # 线程池实现
- logs(dir)                         # 日志文件存放文件夹，启用线程池日志记录后会自动生存
- README.md                         # 项目介绍文档
- scripts(dir)                      # 构建脚本存放目录
- - deply_linux.sh
- - deply_windows.bat
- src(dir)                          # 源码目录
- - logger.cpp                      # 日志系统源码
- - thread_pool_config.cpp          # 线程池配置系统源码
- - thread_pool_improved.cpp        # 线程池源码
- test(dir)                         # 测试目录
- - CMakeLists.txt                  # 测试文件 CMake 构建配置
- - unit(dir)                       # 单元测试
- - - circular_queue_test.cpp       # 循环队列与MPMC阻塞队列测试
- - - thread_pool_config_test.cpp   # 线程池配置系统测试
- - - thread_pool_test.cpp          # 线程池功能与性能基准集成测试
```

## 快速开始

### Linux环境

Linux环境请确保系统变量中已设置`$VCPKG_ROOT`变量，并将值设为`vcpkg`根目录。
确认系统变量设置正确后，在项目根目录运行如下指令：
```bash
./scripts/build_linux.sh
```

### Windows环境

Windows环境请确保系统变量中也设置了`%VCPKG_ROOT%`。
Windows下默认是用`MSVC`编译器，如果要使用`MinGW G++`，请确保系统变量中设置了`%MINGW_ROOT%`变量，仅需设置到`MinGW`根目录即可，无须深入到`bin`文件夹。
确认系统变量设置正确后，在项目根目录运行如下指令：
```bat
.\scripts\build_windows.bat
```

## 项目主要特性

- **高性能线程池**：高效，支持动态任务类型的线程池实现，支持动态管理线程数目
- **MPMC阻塞队列**：多线程安全的阻塞队列
- **循环队列**：高效内存管理数据结构
- **日志系统**：完整的日志记录便于溯源 bug
- **配置系统**：支持 JSON 文件对线程池参数进行配置
- **单元测试**：全面的功能测试

## 配置信息
如需对线程池参数进行配置，可以进入`config`文件夹内进行配置
- `config.json`为线程池配置参数。
- `logger_config.json`为线程池日志记录器配置参数。
- `default_config.json`，`high_throughput.json`，`low_latency.json`，`limit_source.json`为一些建议配置，可对应不同场景使用。
- `throughput_benchmark.json`为性能基准测试所用配置。

## 开发环境

- C++ 编译器支持 C++17 或更高版本
- CMake 3.20 或更高版本
- vcpkg 包管理器