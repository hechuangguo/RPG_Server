cmake_minimum_required(VERSION 3.16)
project(RPG_Server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
# 全局编译选项
# -------------------------------------------------------
add_compile_options(-Wall -Wextra -O2 -g)

# -------------------------------------------------------
# 第三方库路径
# -------------------------------------------------------
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/3Party)

# Lua 5.4（从系统或 3Party 查找）
find_package(Lua 5.4 QUIET)
if(NOT LUA_FOUND)
    set(LUA_INCLUDE_DIR ${THIRD_PARTY_DIR}/lua/include)
    set(LUA_LIBRARIES   ${THIRD_PARTY_DIR}/lua/lib/liblua.a)
endif()

# tinyxml2
find_library(TINYXML2_LIB tinyxml2 PATHS ${THIRD_PARTY_DIR}/tinyxml2/lib)
if(NOT TINYXML2_LIB)
    set(TINYXML2_LIB tinyxml2)
endif()
find_path(TINYXML2_INCLUDE tinyxml2.h PATHS ${THIRD_PARTY_DIR}/tinyxml2/include /usr/include)

# MySQL client
find_path(MYSQL_INCLUDE mysql/mysql.h PATHS /usr/include /usr/local/include)
find_library(MYSQL_LIB mysqlclient PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)

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

# -------------------------------------------------------
# 各服务器
# -------------------------------------------------------
add_server(SuperServer    "")
add_server(SessionServer  "")
add_server(RecordServer   "${MYSQL_LIB}")
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
