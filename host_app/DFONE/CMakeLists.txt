cmake_minimum_required(VERSION 3.16)
project(dfone_host LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(DFONE_BUILD_SHARED "Build dfone_host as a shared library" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(dfone_target_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(DFONE_HOST_LIBRARY_TYPE STATIC)
if (DFONE_BUILD_SHARED)
    set(DFONE_HOST_LIBRARY_TYPE SHARED)
endif()

add_library(dfone_host ${DFONE_HOST_LIBRARY_TYPE}
    src/public/maintenance.cpp
    src/public/session.cpp
    src/transport/control_client.cpp
    src/transport/stream_client.cpp
)

target_include_directories(dfone_host
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common/include
)

dfone_target_warnings(dfone_host)

if (DFONE_BUILD_SHARED)
    target_compile_definitions(dfone_host PRIVATE DFONE_BUILDING_LIBRARY)
else()
    target_compile_definitions(dfone_host PUBLIC DFONE_STATIC)
endif()

if (WIN32)
    target_link_libraries(dfone_host PUBLIC ws2_32)
endif()

install(TARGETS dfone_host
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY public/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
