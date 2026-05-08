# Copyright 2024-2026 Ronny Brendel
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(macos-core-to-core-latency)
enable_language(CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

set(CMAKE_CXX_FLAGS_DEBUG          "-DDEBUG  -g     -mcpu=apple-m1 -fsanitize=address -fsanitize=undefined")
set(CMAKE_CXX_FLAGS_RELEASE        "-DNDEBUG    -O3 -mcpu=apple-m1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-DNDEBUG -g -O3 -mcpu=apple-m1 -fno-omit-frame-pointer -DRELWITHDEBINFO")

add_executable(macos-core-to-core-latency)
target_sources(macos-core-to-core-latency PRIVATE main.cpp)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/macos-core-to-core-latency.py"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/macos-core-to-core-latency.py"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/macos-core-to-core-latency.py" "${CMAKE_CURRENT_BINARY_DIR}/macos-core-to-core-latency.py"
)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/requirements.txt"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt" "${CMAKE_CURRENT_BINARY_DIR}/requirements.txt"
)
add_custom_target(copy_dependencies
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/macos-core-to-core-latency.py" "${CMAKE_CURRENT_BINARY_DIR}/requirements.txt")
add_dependencies(macos-core-to-core-latency copy_dependencies)

install(TARGETS macos-core-to-core-latency DESTINATION .)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/macos-core-to-core-latency.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt"
    DESTINATION .)

find_package(Git)
set(MY_VERSION "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE MY_VERSION
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

set(CPACK_GENERATOR             "ZIP")
set(CPACK_PACKAGE_NAME          "macos-core-to-core-latency")
set(CPACK_PACKAGE_VENDOR        "Ronny Brendel")
set(CPACK_PACKAGE_VERSION       "${MY_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
include(CPack)
