# oobsum-core-tests：core/ 的单元测试（oobsum/specs/SPEC-0004；06- §9.3）。
# 独立工程，链接本仓库构建目录里的 LLVMSupport；core/ 不依赖 Clang，所以只要 LLVM Support 就能链接
# （core/ 误包含 clang/ 头文件时这里会编不过——多一道分层检查）。
# 用法：oobsum/devenv/core-tests.sh [过滤子串]
cmake_minimum_required(VERSION 3.13)
project(oobsum_core_tests CXX)

find_package(LLVM REQUIRED CONFIG)
set(OOBSUM_CHECKERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../clang/lib/StaticAnalyzer/Checkers" CACHE PATH
    "clang/lib/StaticAnalyzer/Checkers（core/ 源码所在）")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
if(NOT LLVM_ENABLE_RTTI)
  add_compile_options(-fno-rtti)
endif()
add_compile_options(-fno-exceptions -Wall -Wextra -Wno-unused-parameter)
# 与 LLVM 自己的构建一致：core/ 里 "return std::move(object);"（Object → Value）为照顾老编译器保留 move
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wredundant-move OOBSUM_HAS_REDUNDANT_MOVE)
if(OOBSUM_HAS_REDUNDANT_MOVE)
  add_compile_options(-Wno-redundant-move)
endif()

file(GLOB OOBSUM_CORE_SRCS CONFIGURE_DEPENDS "${OOBSUM_CHECKERS_DIR}/OobSummary/core/*.cpp")
file(GLOB OOBSUM_TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*Test.cpp")

add_executable(oobsum-core-tests Test.cpp ${OOBSUM_TEST_SRCS} ${OOBSUM_CORE_SRCS})
target_include_directories(oobsum-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OOBSUM_CHECKERS_DIR}
                           ${LLVM_INCLUDE_DIRS})
target_link_libraries(oobsum-core-tests PRIVATE LLVMSupport)
