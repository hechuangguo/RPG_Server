cmake_minimum_required(VERSION 3.16)
project(RPG_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
# 全局编译选项
# -------------------------------------------------------
add_compile_options(-Wall -Wextra -O2 -g)

# -------------------------------------------------------
# 第三方库路径（优先 3Party 本地静态库）
# -------------------------------------------------------
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/3Party)

# --- Lua 5.4 ---
set(LUA_INCLUDE_DIR "${THIRD_PARTY_DIR}/lua/include")
set(LUA_LIBRARIES   "${THIRD_PARTY_DIR}/lua/lib/liblua.a")
if(NOT EXISTS "${LUA_LIBRARIES}")
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

# --- tinyxml2 ---
set(TINYXML2_INCLUDE "${THIRD_PARTY_DIR}/tinyxml2/include")
set(TINYXML2_LIB     "${THIRD_PARTY_DIR}/tinyxml2/lib/libtinyxml2.a")
if(NOT EXISTS "${TINYXML2_LIB}")
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

# --- MySQL client (MariaDB Connector/C in 3Party) ---
set(MYSQL_INCLUDE "${THIRD_PARTY_DIR}/mysql/include")
set(MYSQL_LIB     "${THIRD_PARTY_DIR}/mysql/lib/libmariadbclient.a")
if(NOT EXISTS "${MYSQL_LIB}")
    set(MYSQL_LIB "${THIRD_PARTY_DIR}/mysql/lib/libmariadb.a")
endif()
set(MYSQL_EXTRA_LIBS "")
if(EXISTS "${MYSQL_LIB}")
    message(STATUS "MySQL client: ${MYSQL_LIB}")
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
else()
    find_path(MYSQL_INCLUDE_SYS mysql/mysql.h
        PATHS /usr/include /usr/local/include /usr/include/mysql)
    find_library(MYSQL_LIB_SYS mysqlclient mariadb
        PATHS /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/local/lib)
    if(MYSQL_INCLUDE_SYS AND MYSQL_LIB_SYS)
        set(MYSQL_INCLUDE ${MYSQL_INCLUDE_SYS})
        set(MYSQL_LIB ${MYSQL_LIB_SYS})
        message(STATUS "MySQL client: system ${MYSQL_LIB}")
    else()
        message(FATAL_ERROR
            "MySQL client not found. Run: ./3Party/download_and_build.sh")
    endif()
endif()

# -------------------------------------------------------
# 公共头文件目录
# -------------------------------------------------------
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/sdk
    ${LUA_INCLUDE_DIR}
    ${TINYXML2_INCLUDE}
    ${MYSQL_INCLUDE}
)

# -------------------------------------------------------
# 辅助宏：生成单个服务器可执行文件
# -------------------------------------------------------
macro(add_server SERVER_NAME EXTRA_LIBS)
    file(GLOB SERVER_SRC
        "${CMAKE_SOURCE_DIR}/${SERVER_NAME}/*.cpp"
        "${CMAKE_SOURCE_DIR}/${SERVER_NAME}/*.cc"
    )
    add_executable(${SERVER_NAME} ${SERVER_SRC})
    target_link_libraries(${SERVER_NAME}
        PRIVATE
        pthread
        ${LUA_LIBRARIES}
        ${TINYXML2_LIB}
        ${EXTRA_LIBS}
    )
    set_target_properties(${SERVER_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endmacro()

# RecordServer 需要 MySQL 客户端及 SSL/z 依赖
set(RECORD_SERVER_LIBS ${MYSQL_LIB} ${MYSQL_EXTRA_LIBS})

# -------------------------------------------------------
# 各服务器
# -------------------------------------------------------
add_server(SuperServer    "")
add_server(SessionServer  "")
add_server(RecordServer   "${RECORD_SERVER_LIBS}")
add_server(AOIServer      "")
add_server(SceneServer    "${LUA_LIBRARIES}")
add_server(GatewayServer  "")
add_server(LoggerServer   "")
add_server(GlobalServer   "")
add_server(ZoneServer     "")

# -------------------------------------------------------
# 安装目标（make install）
# -------------------------------------------------------
install(TARGETS
    SuperServer SessionServer RecordServer AOIServer
    SceneServer GatewayServer LoggerServer GlobalServer ZoneServer
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/ DESTINATION config)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/script/ DESTINATION script)
