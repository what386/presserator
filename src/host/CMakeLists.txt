cmake_minimum_required(VERSION 3.16)
project(presserator_host LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

add_executable(presserator-host stdin.cpp sender.cpp bluetooth.cpp ../protocol.cpp)
target_include_directories(presserator-host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(presserator-host PRIVATE cxx_std_17)
target_compile_options(presserator-host PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(presserator-host PRIVATE PkgConfig::GIO)
