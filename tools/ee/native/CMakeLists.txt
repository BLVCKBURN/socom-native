cmake_minimum_required(VERSION 3.20)
project(socom_ee_native_tools LANGUAGES CXX)

add_executable(socom_retail_chain_analyzer recover_retail_execution_chain.cpp)
target_compile_features(socom_retail_chain_analyzer PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(socom_retail_chain_analyzer PRIVATE /W4 /permissive-)
else()
    target_compile_options(socom_retail_chain_analyzer PRIVATE -Wall -Wextra -Wpedantic)
endif()
