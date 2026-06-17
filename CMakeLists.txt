# =============================================================================
#  CMakeLists.txt  —  RPG 游戏服务器项目构建配置
# =============================================================================
#  项目说明：
#    本 CMake 项目构建一套分布式 RPG 游戏服务器集群，包含多个独立的服务器进程。
#    各服务器进程之间通过 TCP 进行内部通信，共同协作完成游戏逻辑。
#
#    编译要求：CMake >= 3.16, C++17, GCC/Clang 或 MSVC
#    构建方式：mkdir build && cd build && cmake .. && make -j$(nproc)
#             或使用项目提供的 build.sh 脚本
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(RPG_Server CXX)

# -------------------------------------------------------
# C++ 标准设置
# 使用 C++17 标准（需要 filesystem / optional / variant 等特性）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
# 全局编译选项
# -Wall      : 启用所有常见警告，帮助发现潜在问题
# -Wextra    : 启用额外警告（未使用参数等），提高代码质量
# -O2        : Release 优化级别2，平衡性能和编译速度
# -g         : 保留调试符号，支持生产环境 core dump 分析
add_compile_options(-Wall -Wextra -O2 -g)

# -------------------------------------------------------
# 第三方库路径配置（优先使用 3Party 目录下的本地预编译静态库）
# 本地库优先策略：开发团队统一管理依赖版本，避免系统库版本不一致导致的问题
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/3Party)

# --- Lua 5.4 脚本引擎 ---
# 用途：SceneServer 使用 Lua 驱动游戏业务逻辑（技能效果/NPC对话/任务触发等）
#       Lua 脚本可热更新，无需重启服务器即可修改游戏规则
set(LUA_INCLUDE_DIR "${THIRD_PARTY_DIR}/lua/include")
set(LUA_LIBRARIES   "${THIRD_PARTY_DIR}/lua/lib/liblua.a")
if(NOT EXISTS "${LUA_LIBRARIES}")
    # 本地库不存在时尝试查找系统安装的 Lua 包
    find_package(Lua 5.4 QUIET)
    if(LUA_FOUND)
        message(STATUS "Lua: system package")
    else()
        message(FATAL_ERROR
            "Lua not found. Run: ./3Party/download_and_build.sh")
    endif()
else()
    message(STATUS "Lua: ${LUA_LIBRARIES}")
endif()

# --- tinyxml2 XML解析库 ---
# 用途：所有服务器均使用 tinyxml2 解析 XML 配置文件（config.xml / server_info.xml 等）
#       轻量级、无外部依赖的 XML 解析方案
set(TINYXML2_INCLUDE "${THIRD_PARTY_DIR}/tinyxml2/include")
set(TINYXML2_LIB     "${THIRD_PARTY_DIR}/tinyxml2/lib/libtinyxml2.a")
if(NOT EXISTS "${TINYXML2_LIB}")
    # 本地库不存在时尝试查找系统安装的 tinyxml2
    find_library(TINYXML2_LIB_SYS tinyxml2)
    find_path(TINYXML2_INCLUDE_SYS tinyxml2.h)
    if(TINYXML2_LIB_SYS AND TINYXML2_INCLUDE_SYS)
        set(TINYXML2_LIB ${TINYXML2_LIB_SYS})
        set(TINYXML2_INCLUDE ${TINYXML2_INCLUDE_SYS})
        message(STATUS "tinyxml2: system ${TINYXML2_LIB}")
    else()
        message(FATAL_ERROR
            "tinyxml2 not found. Run: ./3Party/download_and_build.sh")
    endif()
else()
    message(STATUS "tinyxml2: ${TINYXML2_LIB}")
endif()

# --- MySQL 客户端库 ---
# 优先系统 libmysqlclient（兼容 MySQL 8.4 caching_sha2_password）；
# 回退 3Party 静态 MariaDB Connector/C（离线/无系统库环境）。
set(MYSQL_INCLUDE "${THIRD_PARTY_DIR}/mysql/include")
set(MYSQL_EXTRA_LIBS "")
find_library(MYSQL_LIB_SYS
    NAMES mysqlclient libmysqlclient.so.24
    PATHS /usr/lib64/mysql /usr/lib/mysql /usr/lib64 /usr/lib
    NO_DEFAULT_PATH)
if(NOT MYSQL_LIB_SYS AND EXISTS "/usr/lib64/mysql/libmysqlclient.so.24")
    set(MYSQL_LIB_SYS "/usr/lib64/mysql/libmysqlclient.so.24")
endif()
if(MYSQL_LIB_SYS)
    set(MYSQL_LIB ${MYSQL_LIB_SYS})
    list(APPEND MYSQL_EXTRA_LIBS dl)
    find_library(CRYPT_LIB crypt)
    if(CRYPT_LIB)
        list(APPEND MYSQL_EXTRA_LIBS ${CRYPT_LIB})
    endif()
    message(STATUS "MySQL client: system ${MYSQL_LIB}")
else()
    set(MYSQL_LIB "${THIRD_PARTY_DIR}/mysql/lib/libmariadbclient.a")
    if(NOT EXISTS "${MYSQL_LIB}")
        set(MYSQL_LIB "${THIRD_PARTY_DIR}/mysql/lib/libmariadb.a")
    endif()
    if(NOT EXISTS "${MYSQL_LIB}")
        message(FATAL_ERROR
            "MySQL client not found. Run: ./3Party/download_and_build.sh")
    endif()
    message(STATUS "MySQL client: bundled ${MYSQL_LIB}")
    find_package(OpenSSL QUIET)
    find_library(ZLIB_LIB z)
    if(OpenSSL_FOUND)
        list(APPEND MYSQL_EXTRA_LIBS OpenSSL::SSL OpenSSL::Crypto)
    else()
        find_library(SSL_LIB ssl)
        find_library(CRYPTO_LIB crypto)
        if(SSL_LIB AND CRYPTO_LIB)
            list(APPEND MYSQL_EXTRA_LIBS ${SSL_LIB} ${CRYPTO_LIB})
        endif()
    endif()
    if(ZLIB_LIB)
        list(APPEND MYSQL_EXTRA_LIBS ${ZLIB_LIB})
    endif()
    list(APPEND MYSQL_EXTRA_LIBS dl)
    find_library(CRYPT_LIB crypt)
    if(CRYPT_LIB)
        list(APPEND MYSQL_EXTRA_LIBS ${CRYPT_LIB})
    endif()
endif()

# -------------------------------------------------------
# 公共头文件搜索路径
# 所有服务器均可直接引用以下目录中的头文件
include_directories(
    ${CMAKE_SOURCE_DIR}           # 项目根目录（公共头文件如 Protocol/ Common/ 等）
    ${CMAKE_SOURCE_DIR}/sdk       # SDK 目录（封装的网络协议/序列化等基础工具）
    ${LUA_INCLUDE_DIR}            # Lua 头文件路径
    ${TINYXML2_INCLUDE}           # tinyxml2 头文件路径
    ${MYSQL_INCLUDE}              # MySQL 客户端头文件路径
)

# -------------------------------------------------------
# 辅助宏：add_server — 生成单个服务器可执行文件的通用模板
# 参数说明：
#   $SERVER_NAME : 服务器名称（同时也是源代码子目录名和可执行文件名）
#   $EXTRA_LIBS  : 该服务器额外需要的链接库（除 pthread/Lua/tinyxml2 之外的库）
# 工作流程：
#   1. 扫描 SERVER_NAME 目录下的所有 .cpp/.cc 源文件
#   2. 创建可执行目标
#   3. 链接公共依赖（pthread + Lua + tinyxml2）+ 额外依赖
#   4. 设置输出目录为各服务器源码目录（便于单服直接启动）
# -------------------------------------------------------
macro(add_server SERVER_NAME EXTRA_LIBS)
    file(GLOB SERVER_SRC
        "${CMAKE_SOURCE_DIR}/${SERVER_NAME}/*.cpp"
        "${CMAKE_SOURCE_DIR}/${SERVER_NAME}/*.cc"
    )
    # SDK 公共实现源（如 sdk/math/*.cpp）：随各服务器一并编译链接
    file(GLOB_RECURSE SDK_SRC "${CMAKE_SOURCE_DIR}/sdk/*.cpp")
    add_executable(${SERVER_NAME} ${SERVER_SRC} ${SDK_SRC})
    target_link_libraries(${SERVER_NAME}
        PRIVATE
        pthread          # POSIX 线程库（所有服务器均需多线程支持）
        ${LUA_LIBRARIES} # Lua 脚本引擎（SceneServer 必需，其他可选）
        ${TINYXML2_LIB}  # XML 配置解析库（所有服务器均需读取配置文件）
        ${EXTRA_LIBS}    # 服务器特有的额外链接库
    )
    set_target_properties(${SERVER_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/${SERVER_NAME}  # 可执行文件输出到各服务器目录
    )
endmacro()

# RecordServer 特殊依赖：MySQL 客户端及 SSL/zlib 传输层库
set(RECORD_SERVER_LIBS ${MYSQL_LIB} ${MYSQL_EXTRA_LIBS})

# -------------------------------------------------------
# 各服务器目标定义
# 每个服务器是一个独立的可执行程序，运行在各自的进程中
# 通过 TCP Socket 相互通信形成分布式集群
# -------------------------------------------------------
add_server(SuperServer    "${RECORD_SERVER_LIBS}")     # SuperServer：中心路由服务器；启动期只读 MySQL 加载 ServerList 拓扑
add_server(SessionServer  "${RECORD_SERVER_LIBS}")     # SessionServer：Relation 经 Record；直连 rpg_game
add_server(RecordServer   "${RECORD_SERVER_LIBS}")     # RecordServer：数据记录服务器，唯一连接MySQL的节点，代理所有DB操作
add_server(AOIServer      "")                          # AOIServer：兴趣范围（Area Of Interest）服务器，9宫格算法管理视野
add_server(SceneServer    "${LUA_LIBRARIES}")          # SceneServer：场景逻辑服务器，Lua脚本驱动战斗/NPC/任务等游戏玩法
add_server(GatewayServer  "")                          # GatewayServer：网关服务器，客户端TCP接入入口，消息分发和会话维持
add_server(LoggerServer   "")                          # LoggerServer：日志服务器，集中收集各节点的远程日志并写入文件
add_server(GlobalServer   "${RECORD_SERVER_LIBS}")     # GlobalServer：全区数据 + 可选 MySQL/HTTP（extern_global.xml）
add_server(ZoneServer     "${RECORD_SERVER_LIBS}")     # ZoneServer：跨区转发 + 可选 MySQL（extern_zone.xml）
add_server(LoginServer    "${RECORD_SERVER_LIBS}")     # LoginServer：外联登录验证 + 网关列表（extern_login.xml）

# -------------------------------------------------------
# 安装目标（make install）
# 将编译产物部署到指定目录
# -------------------------------------------------------
install(TARGETS
    SuperServer SessionServer RecordServer AOIServer
    SceneServer GatewayServer LoggerServer GlobalServer ZoneServer LoginServer
    RUNTIME DESTINATION bin         # 所有可执行文件安装到 <prefix>/bin/
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/ DESTINATION config)  # 配置文件安装到 <prefix>/config/
install(DIRECTORY ${CMAKE_SOURCE_DIR}/script/ DESTINATION script)  # Lua脚本安装到 <prefix>/script/
install(DIRECTORY ${CMAKE_SOURCE_DIR}/database/ DESTINATION database
        FILES_MATCHING PATTERN "*.lua")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/tables/ DESTINATION tables
        FILES_MATCHING PATTERN "*.sql")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/basefile/ DESTINATION basefile)
