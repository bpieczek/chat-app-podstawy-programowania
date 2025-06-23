cmake_minimum_required(VERSION 3.10)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_executable(ChatServer
    main.cpp
    ChatServer.cpp
    ClientHandler.cpp
    Message.cpp
    Logger.cpp
)

target_include_directories(ChatServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ChatServer pthread)
