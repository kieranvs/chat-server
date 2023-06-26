cmake_minimum_required(VERSION 3.12)
project(SocketDemo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(server server.cpp)
target_link_libraries(server pthread)

add_executable(client client.cpp)