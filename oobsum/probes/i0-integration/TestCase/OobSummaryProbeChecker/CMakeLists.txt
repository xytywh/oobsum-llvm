INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# .c 是当前 2 个用例 TU（跨 TU 调用 + 函数指针 + 循环 + 递归 + 头文件 static inline），.cpp 预留
file(GLOB OSP_TC_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
add_library(TC_OobSummaryProbeChecker
        ${OSP_TC_SOURCES}
)
