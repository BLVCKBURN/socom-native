cmake_minimum_required(VERSION 3.20)
project(socom_native_runtime LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Single-player runtime is currently Windows-only. The data/export tools remain portable.")
endif()

add_executable(socom-native
    src/main_win32.cpp
    src/runtime_data.hpp
    src/player_data.hpp
)

add_executable(socom_scene_gltf tools/socom_scene_gltf.cpp)
add_executable(socom_collision_gltf tools/socom_collision_gltf.cpp)
add_executable(build_m8_runtime_pack tools/build_m8_runtime_pack.cpp)
add_executable(runtime_pack_check tools/runtime_pack_check.cpp src/runtime_data.hpp)
add_executable(socom_texture_tool tools/socom_texture_tool.cpp)
add_executable(socom_character_gltf tools/socom_character_gltf.cpp)
add_executable(build_player_runtime_pack tools/build_player_runtime_pack.cpp)
add_executable(player_pack_check tools/player_pack_check.cpp src/player_data.hpp src/runtime_data.hpp)
add_executable(socom_frontend WIN32 tools/socom_frontend.cpp)

target_include_directories(runtime_pack_check PRIVATE src)
target_include_directories(player_pack_check PRIVATE src)

foreach(target
    socom-native
    socom_scene_gltf
    socom_collision_gltf
    build_m8_runtime_pack
    runtime_pack_check
    socom_texture_tool
    socom_character_gltf
    build_player_runtime_pack
    player_pack_check
    socom_frontend)
    target_compile_features(${target} PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

target_compile_definitions(socom-native PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(socom-native PRIVATE opengl32 glu32 user32 gdi32)
target_compile_definitions(socom_frontend PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(socom_frontend PRIVATE user32 gdi32 shell32)

set_target_properties(
    socom-native
    socom_scene_gltf
    socom_collision_gltf
    build_m8_runtime_pack
    runtime_pack_check
    socom_texture_tool
    socom_character_gltf
    build_player_runtime_pack
    player_pack_check
    socom_frontend
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
