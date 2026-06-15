cmake_minimum_required(VERSION 3.16)
project(dfone_customer_capture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DFONE_SDK_ROOT "" CACHE PATH "Path to DFONE Windows SDK static or shared directory")
option(DFONE_USE_SHARED "Link the shared DFONE SDK instead of the static SDK" OFF)

if (NOT DFONE_SDK_ROOT)
    message(FATAL_ERROR "Set DFONE_SDK_ROOT to the package sdk/static or sdk/shared directory")
endif()

get_filename_component(DFONE_SDK_ROOT "${DFONE_SDK_ROOT}" ABSOLUTE)
set(DFONE_INCLUDE_DIR "${DFONE_SDK_ROOT}/include")
set(DFONE_LIBRARY "${DFONE_SDK_ROOT}/lib/dfone_host.lib")
set(DFONE_DLL "${DFONE_SDK_ROOT}/bin/dfone_host.dll")

if (NOT EXISTS "${DFONE_INCLUDE_DIR}/dfone/session.hpp")
    message(FATAL_ERROR "DFONE headers not found under ${DFONE_INCLUDE_DIR}")
endif()
if (NOT EXISTS "${DFONE_LIBRARY}")
    message(FATAL_ERROR "DFONE library not found: ${DFONE_LIBRARY}")
endif()
if (DFONE_USE_SHARED AND NOT EXISTS "${DFONE_DLL}")
    message(FATAL_ERROR "DFONE DLL not found: ${DFONE_DLL}")
endif()

add_executable(dfone_customer_capture main.cpp)
target_include_directories(dfone_customer_capture PRIVATE "${DFONE_INCLUDE_DIR}")
target_link_libraries(dfone_customer_capture PRIVATE "${DFONE_LIBRARY}" ws2_32)

if (NOT DFONE_USE_SHARED)
    target_compile_definitions(dfone_customer_capture PRIVATE DFONE_STATIC)
else()
    add_custom_command(TARGET dfone_customer_capture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DFONE_DLL}"
            "$<TARGET_FILE_DIR:dfone_customer_capture>"
    )
endif()
