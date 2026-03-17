cmake_minimum_required(VERSION 3.10)
project(StairClimbingSimulator C)

set(CMAKE_C_STANDARD 99)

# 查找线程库
find_package(Threads REQUIRED)

add_executable(server
        server.c
        globals.c
        web_server.c
        simulator.c
        db_manager.c
        audio_hint.c
        cjson.c
        sqlite3.c
)

# ✅ 关键修复：链接必要的系统库
target_link_libraries(server PRIVATE
        Threads::Threads
        ws2_32
)

# 可选：启用详细输出便于调试
set(CMAKE_VERBOSE_MAKEFILE ON)