cmake_minimum_required(VERSION 3.24)
project(sourcesight_windows VERSION 1.0.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SourceSight Windows requires a 64-bit Windows toolchain.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(src/external/glfw EXCLUDE_FROM_ALL)

include(FetchContent)
FetchContent_Declare(glew
    URL https://github.com/nigels-com/glew/releases/download/glew-2.3.1/glew-2.3.1.zip
    URL_HASH SHA256=09e0083ae46930aba9b53e72c92ee1a557e24ed393526fec26cb0ebabd834720
    SOURCE_SUBDIR build/cmake)
FetchContent_MakeAvailable(glew)
set_property(DIRECTORY "${glew_SOURCE_DIR}/build/cmake" PROPERTY EXCLUDE_FROM_ALL TRUE)
find_package(OpenGL REQUIRED)

include(cmake/VisCheckCS2.cmake)
if(MINGW)
    target_link_options(VPhysToOpt PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui")
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER APP_SOURCES EXCLUDE REGEX "/external/")
list(REMOVE_ITEM APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(sourcesight_core STATIC ${APP_SOURCES}
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    src/external/AsyncLogger/src/LogCapture.cpp
    src/external/AsyncLogger/src/Logger.cpp
    src/external/AsyncLogger/src/LogIntermediate.cpp
    src/external/AsyncLogger/src/LogMessage.cpp
    src/external/AsyncLogger/src/LogStream.cpp)
target_include_directories(sourcesight_core PUBLIC src src/external ${IMGUI_DIR}
    src/external/json/include src/external/AsyncLogger/src
    src/external/AsyncLogger/include/AsyncLogger src/external/VisCheckCS2/VisCheckCS2
    ${glew_SOURCE_DIR}/include)
target_compile_definitions(sourcesight_core PUBLIC NOMINMAX IMGUI_DEFINE_MATH_OPERATORS
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 GLEW_STATIC GLFW_INCLUDE_NONE
    SOURCESIGHT_VERSION="${PROJECT_VERSION}")
target_precompile_headers(sourcesight_core PUBLIC src/common.hpp)
target_link_libraries(sourcesight_core PUBLIC OpenGL::GL glfw glew_s
    VisCheckCS2 user32 gdi32 dwmapi psapi advapi32)
if(MSVC)
    target_compile_options(sourcesight_core PRIVATE /W3 /utf-8 /permissive-)
else()
    target_compile_options(sourcesight_core PRIVATE -Wall -Wextra -Wno-unknown-pragmas)
    target_link_options(sourcesight_core PUBLIC -static -static-libgcc -static-libstdc++)
endif()
add_executable(sourcesight src/main.cpp)
target_link_libraries(sourcesight PRIVATE sourcesight_core)
install(TARGETS sourcesight VPhysToOpt RUNTIME DESTINATION .)
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION .)
install(FILES assets/brand/menu.png DESTINATION assets/brand)

option(SOURCESIGHT_BUILD_TESTS "Build offline regression tests" ON)
if(SOURCESIGHT_BUILD_TESTS)
    enable_testing()
    foreach(name config runtime menu radar full-map-renderer windows overlay-composition)
        add_executable(test-${name} tools/test-${name}.cpp)
        target_link_libraries(test-${name} PRIVATE sourcesight_core)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/${name}")
        add_test(NAME ${name} COMMAND test-${name})
        set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests/${name}" TIMEOUT 60)
    endforeach()
    add_executable(test-map-geometry tools/test-map-geometry.cpp)
    target_link_libraries(test-map-geometry PRIVATE sourcesight_core)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests/map-geometry")
    add_test(NAME map-geometry COMMAND test-map-geometry --synthetic)
    set_tests_properties(map-geometry PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests/map-geometry" TIMEOUT 120)
    add_test(NAME preview COMMAND sourcesight --preview --frames 12)
    set_tests_properties(preview PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests" TIMEOUT 60)
endif()
