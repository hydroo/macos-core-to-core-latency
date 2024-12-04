cmake_minimum_required(VERSION 3.20)
project(macos-core-to-core-latency)
enable_language(CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

set(CMAKE_CXX_FLAGS_DEBUG          "-DDEBUG  -g -fsanitize=address -fsanitize=undefined")
set(CMAKE_CXX_FLAGS_RELEASE        "-DNDEBUG    -O3 -march=native -mtune=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-DNDEBUG -g -O3 -march=native -mtune=native -fno-omit-frame-pointer -DRELWITHDEBINFO")

add_executable(macos-core-to-core-latency)
target_sources(macos-core-to-core-latency PRIVATE main.cpp)
