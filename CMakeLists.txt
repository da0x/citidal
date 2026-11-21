cmake_minimum_required(VERSION 3.28)
project(citadel LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES
        source/platform/linux/install.cpp
        source/platform/linux/process.cpp)
else()
    message(FATAL_ERROR "citadel builds on Linux for now. Windows is next.")
endif()

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06) # v1.91.9b
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799)
FetchContent_MakeAvailable(imgui stb)

find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

add_library(stb STATIC source/image/stb.c)
target_include_directories(stb SYSTEM PUBLIC ${stb_SOURCE_DIR})

add_library(citadel_core STATIC
    ${PLATFORM_SOURCES}
    source/image/image.cpp
    source/pack/pack.cpp
    source/engine/discover.cpp
    source/engines/source-engine/vpk.cpp
    source/engines/source-engine/vtf.cpp
    source/engines/source-engine/content.cpp
    source/engines/source-engine/material.cpp
    source/engines/source-engine/maps.cpp
    source/engines/source-engine/half-life-2.cpp)
target_include_directories(citadel_core PUBLIC source)
target_link_libraries(citadel_core PUBLIC stb nlohmann_json::nlohmann_json)
target_compile_options(citadel_core PRIVATE -Wall -Wextra)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)

add_executable(citadel
    source/main.cpp
    source/ui/ui.cpp
    source/studio/studio.cpp)
target_link_libraries(citadel PRIVATE citadel_core imgui)
target_compile_options(citadel PRIVATE -Wall -Wextra)

add_executable(citadel_check tests/check.cpp)
target_link_libraries(citadel_check PRIVATE citadel_core)
