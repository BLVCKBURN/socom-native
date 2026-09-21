cmake_minimum_required(VERSION 3.20)
project(socom_menu_boot LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "socom_menu_boot is currently a Windows-native bootstrap target.")
endif()

add_executable(socom_menu_boot WIN32
    main.cpp
)

target_compile_features(socom_menu_boot PRIVATE cxx_std_17)
target_compile_definitions(socom_menu_boot PRIVATE UNICODE _UNICODE)
target_link_libraries(socom_menu_boot PRIVATE shell32)

if(MSVC)
    target_compile_options(socom_menu_boot PRIVATE /W4 /permissive-)
endif()
