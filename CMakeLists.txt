cmake_minimum_required(VERSION 3.20)
project(mrq CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SQLite3 REQUIRED)

add_executable(mrq
  src/main.cpp src/core.cpp src/db.cpp src/ingest.cpp
  src/service.cpp src/server.cpp src/client.cpp)
target_include_directories(mrq PRIVATE src)
target_include_directories(mrq SYSTEM PRIVATE third_party)
target_link_libraries(mrq PRIVATE SQLite::SQLite3)
target_compile_options(mrq PRIVATE -Wall -Wextra)
