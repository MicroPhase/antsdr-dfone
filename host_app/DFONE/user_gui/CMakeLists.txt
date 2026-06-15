cmake_minimum_required(VERSION 3.16)
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(dfone_user_apps VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DFONE_USER_BUILD_GUI "Build the public-API GUI example" ON)
option(DFONE_USER_PACKAGE_RUNTIME_DEPS "Copy runtime DLL dependencies during install" ${WIN32})
option(DFONE_USER_STATIC_MSVC_RUNTIME "Use the static MSVC runtime for single-exe Windows distribution" ON)
option(DFONE_USER_STATIC_GLEW "Define GLEW_STATIC for static GLEW builds" ${WIN32})
set(DFONE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Path to the DFONE host SDK root")

set(DFONE_USER_ENABLE_DOA_DEFAULT OFF)
if (EXISTS "${DFONE_ROOT}/factory/src/doa_processing.cpp" AND
    EXISTS "${DFONE_ROOT}/factory/include/dfone/internal/doa_processing.hpp")
    set(DFONE_USER_ENABLE_DOA_DEFAULT ON)
endif()
option(DFONE_USER_ENABLE_DOA "Build the DOA estimation panel into the user GUI" ${DFONE_USER_ENABLE_DOA_DEFAULT})

if (MSVC AND DFONE_USER_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(dfone_user_target_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if (NOT TARGET dfone_host)
    set(DFONE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(DFONE_BUILD_GUI OFF CACHE BOOL "" FORCE)
    set(DFONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DFONE_INSTALL_SDK OFF CACHE BOOL "" FORCE)
    add_subdirectory("${DFONE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/dfone_host")
endif()

add_executable(dfone_user_cli
    src/user_cli.cpp
)

target_link_libraries(dfone_user_cli PRIVATE dfone_host Threads::Threads)
dfone_user_target_warnings(dfone_user_cli)

if (DFONE_USER_BUILD_GUI)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 CONFIG QUIET)
    find_package(GLEW CONFIG QUIET)

    if (NOT TARGET glfw AND NOT TARGET glfw3)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW3 REQUIRED glfw3)
    endif()

    if (NOT TARGET GLEW::GLEW)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLEW REQUIRED glew)
    endif()

    set(IMGUI_DIR "${DFONE_ROOT}/third_party/imgui"
        CACHE PATH "Path to Dear ImGui" FORCE)

    set(DFONE_USER_GUI_TYPE)
    if (WIN32)
        set(DFONE_USER_GUI_TYPE WIN32)
    endif()

    add_executable(dfone_user_gui ${DFONE_USER_GUI_TYPE}
        src/user_gui.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(dfone_user_gui PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${GLFW3_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIRS}
    )

    target_link_directories(dfone_user_gui PRIVATE
        ${GLFW3_LIBRARY_DIRS}
        ${GLEW_LIBRARY_DIRS}
    )

    target_link_libraries(dfone_user_gui PRIVATE dfone_host OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
    if (TARGET glfw)
        target_link_libraries(dfone_user_gui PRIVATE glfw)
    elseif (TARGET glfw3)
        target_link_libraries(dfone_user_gui PRIVATE glfw3)
    else()
        target_link_libraries(dfone_user_gui PRIVATE ${GLFW3_LIBRARIES})
    endif()

    if (TARGET GLEW::GLEW)
        target_link_libraries(dfone_user_gui PRIVATE GLEW::GLEW)
    else()
        target_link_libraries(dfone_user_gui PRIVATE ${GLEW_LIBRARIES})
    endif()

    target_compile_definitions(dfone_user_gui PRIVATE
        IMGUI_IMPL_OPENGL_LOADER_GLEW
        $<$<BOOL:${DFONE_USER_STATIC_GLEW}>:GLEW_STATIC>
    )
    if (DFONE_USER_ENABLE_DOA)
        if (NOT EXISTS "${DFONE_ROOT}/factory/src/doa_processing.cpp")
            message(FATAL_ERROR "DFONE_USER_ENABLE_DOA requires factory/src/doa_processing.cpp")
        endif()
        target_sources(dfone_user_gui PRIVATE
            "${DFONE_ROOT}/factory/src/doa_processing.cpp"
        )
        target_include_directories(dfone_user_gui PRIVATE
            "${DFONE_ROOT}/factory/include"
            "${DFONE_ROOT}/src/common/include"
        )
        target_compile_definitions(dfone_user_gui PRIVATE DFONE_USER_ENABLE_DOA)
    endif()
    dfone_user_target_warnings(dfone_user_gui)
    target_compile_options(dfone_user_gui PRIVATE
        ${GLFW3_CFLAGS_OTHER}
        ${GLEW_CFLAGS_OTHER}
    )

    if (WIN32)
        if (MSVC)
            target_link_options(dfone_user_gui PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        else()
            target_link_options(dfone_user_gui PRIVATE -mwindows)
        endif()
    endif()
endif()

install(TARGETS dfone_user_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (TARGET dfone_user_gui)
    install(TARGETS dfone_user_gui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if (DFONE_USER_PACKAGE_RUNTIME_DEPS AND WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    install(CODE [[
        file(GET_RUNTIME_DEPENDENCIES
            RESOLVED_DEPENDENCIES_VAR resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
            EXECUTABLES
                "$<TARGET_FILE:dfone_user_cli>"
                $<$<TARGET_EXISTS:dfone_user_gui>:"$<TARGET_FILE:dfone_user_gui>">
            PRE_EXCLUDE_REGEXES
                "api-ms-"
                "ext-ms-"
                "^/lib"
                "^/usr/lib"
                "^/usr/lib64"
                "^/lib64"
            POST_EXCLUDE_REGEXES
                ".*system32/.*\\.dll"
                ".*System32/.*\\.dll"
        )
        foreach(dep IN LISTS resolved_deps)
            file(INSTALL
                DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                TYPE SHARED_LIBRARY
                FILES "${dep}"
            )
        endforeach()
        if (unresolved_deps)
            message(WARNING "Unresolved runtime dependencies: ${unresolved_deps}")
        endif()
    ]])
endif()

set(CPACK_PACKAGE_NAME "dfone-user-apps")
set(CPACK_PACKAGE_VENDOR "DFONE")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "DFONE")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DFONE public API CLI and GUI applications")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if (WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
