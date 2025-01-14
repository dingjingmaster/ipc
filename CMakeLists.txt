cmake_minimum_required(VERSION 3.10)
project(lpb C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/3thrd)

find_package(PkgConfig)

pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(QT5 REQUIRED Qt5Core)

add_library(rpc SHARED ipc.h ipc-base.h ipc-base.cc)
target_compile_options(rpc PUBLIC -fPIC)
target_compile_definitions(rpc PUBLIC -D QT_NO_KEYWORDS)
target_link_libraries(rpc PUBLIC ${GIO_LIBRARIES} ${QT5_LIBRARIES})
target_include_directories(rpc PUBLIC ${GIO_INCLUDE_DIRS} ${QT5_INCLUDE_DIRS})
