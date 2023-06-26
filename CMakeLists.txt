cmake_minimum_required(VERSION 3.12)
project(SocketDemo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    set(CMAKE_CXX_FLAGS "/EHsc")
endif(MSVC)

# Server is only supported on linux
if (UNIX)
	add_executable(server server.cpp)
	target_link_libraries(server pthread)
endif()

add_executable(client client.cpp)
if (WIN32)
	target_link_libraries(client Ws2_32.lib Mswsock.lib AdvApi32.lib)
endif()