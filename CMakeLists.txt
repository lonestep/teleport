cmake_minimum_required (VERSION 3.8)

project ("teleport")

message("Platform:${CMAKE_SYSTEM_NAME}")

add_definitions(-D${CMAKE_SYSTEM_NAME})

add_executable (teleport "teleport.cpp" "teleport.hpp" "platform.cpp" "platform.hpp" "unittest.cpp")

if (WIN32 OR MINGW)
    target_link_libraries(teleport ws2_32 shlwapi ole32)
endif()


