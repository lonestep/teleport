cmake_minimum_required (VERSION 3.8)

project ("teleport")

message("Platform:${CMAKE_SYSTEM_NAME}")

add_definitions(-D${CMAKE_SYSTEM_NAME})

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /Oi /Ot /Oy /GL /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
endif()

add_executable (teleport "teleport.cpp" "teleport.hpp" "platform.cpp" "platform.hpp" "unittest.cpp")

if (WIN32 OR MINGW)
    target_link_libraries(teleport ws2_32 shlwapi ole32 winmm)
elseif (UNIX AND NOT APPLE)
    target_link_libraries(teleport pthread rt)
endif()


