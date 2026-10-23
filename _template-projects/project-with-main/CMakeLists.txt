cmake_minimum_required(VERSION 3.25)

####################
# Project
get_filename_component(DIRECTORY_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
string(REPLACE " " "_" TARGET_MAIN ${DIRECTORY_NAME})
set(TARGET_MAIN ${TARGET_MAIN})

project(${TARGET_MAIN} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

####################
# Sources & headers
aux_source_directory(. SRC_LIST)
file(GLOB HEADERS_LIST "*.h" "*.hpp")

####################
# Executable
enable_testing()

add_executable(${TARGET_MAIN} ${SRC_LIST} ${HEADERS_LIST})

target_compile_features(${TARGET_MAIN} PRIVATE cxx_std_23)