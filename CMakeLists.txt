cmake_minimum_required(VERSION 3.15)

# 项目名称和版本
project(NutShellSearch VERSION 1.0)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启所有警告，有助于写出健壮的代码
# 注意: 不再硬编码 -pthread (由 find_package(Threads) 处理)、-g (由 CMAKE_BUILD_TYPE 控制)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 设置可执行文件的输出路径为项目根目录下的 bin 文件夹
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 头文件搜索路径
# /usr/local/include: cppjieba, simhash, utfcpp 等 header-only 库的安装位置
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/local/include
)

# --- 查找系统依赖库 ---
# 查找多线程库
find_package(Threads REQUIRED)

# tinyxml2: 用于离线模块解析 XML 语料
find_package(tinyxml2 REQUIRED)

# nlohmann_json: 用于在线模块 JSON 序列化
add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
target_include_directories(nlohmann_json::nlohmann_json
    INTERFACE /usr/local/include
)

# muduo: 用于在线模块的 TCP 网络框架
add_library(muduo::muduo_base STATIC IMPORTED)
set_target_properties(muduo::muduo_base PROPERTIES
    IMPORTED_LOCATION /usr/local/lib/libmuduo_base.a
)

add_library(muduo::muduo_net STATIC IMPORTED)
set_target_properties(muduo::muduo_net PROPERTIES
    IMPORTED_LOCATION /usr/local/lib/libmuduo_net.a
)
target_link_libraries(muduo::muduo_net INTERFACE muduo::muduo_base)

# =========================================================================
# 模块一：收集公共模块代码 (common)
# =========================================================================
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "src/common/*.cpp")

# =========================================================================
# 模块二：离线处理程序 (Offline Builder)
# =========================================================================
file(GLOB OFFLINE_SOURCES CONFIGURE_DEPENDS "src/offline/*.cpp")

# 生成离线可执行文件 nutshell_offline
add_executable(nutshell_offline ${OFFLINE_SOURCES} ${COMMON_SOURCES})

# 链接离线程序需要的库
target_link_libraries(nutshell_offline
    tinyxml2::tinyxml2
    Threads::Threads
)

# =========================================================================
# 模块三：在线检索服务器 (Online Engine)
# =========================================================================
file(GLOB ONLINE_SOURCES CONFIGURE_DEPENDS "src/online/*.cpp")

# 生成在线可执行文件 nutshell_online
add_executable(nutshell_online ${ONLINE_SOURCES} ${COMMON_SOURCES})

# 链接在线程序需要的库：muduo_net, muduo_base, nlohmann_json, pthread
target_link_libraries(nutshell_online
    muduo::muduo_net
    muduo::muduo_base
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# =========================================================================
# (预留) 单元测试模块
# =========================================================================
# 如果后续有 tests 目录下的测试文件，可以在这里添加编译逻辑
