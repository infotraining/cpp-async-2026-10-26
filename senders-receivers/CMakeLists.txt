cmake_minimum_required(VERSION 3.25)

####################
# Project
get_filename_component(DIRECTORY_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
string(REPLACE " " "_" TARGET_MAIN ${DIRECTORY_NAME})
set(TARGET_MAIN_TESTS ${TARGET_MAIN}_test)

project(${TARGET_MAIN_TESTS} LANGUAGES CXX)

####################
# Dependencies

# Include(FetchContent)

# #############
# # Catch2

# set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

# FetchContent_Declare(
#   Catch2
#   GIT_REPOSITORY https://github.com/catchorg/Catch2.git
#   GIT_TAG        v3.13.0 # or a later release
# )
# FetchContent_MakeAvailable(Catch2)

# #############
# # stdexec
# set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# FetchContent_Declare(
#   stdexec
#   GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
#   GIT_TAG        5076be2b35de2e78330201b888d82c81b8cb428b
# )
# FetchContent_MakeAvailable(stdexec)
# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE STDEXEC::stdexec)


####################
# Sources & headers
aux_source_directory(. SRC_LIST)
file(GLOB HEADERS_LIST "*.h" "*.hpp")

####################
# Executable
enable_testing()

add_executable(${TARGET_MAIN_TESTS} ${SRC_LIST} ${HEADERS_LIST})

target_compile_features(${TARGET_MAIN_TESTS} PRIVATE cxx_std_23)
target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Catch2::Catch2 Catch2::Catch2WithMain STDEXEC::stdexec)

# enable_sanitizers(${TARGET_MAIN_TESTS})

include(CTest)
include(Catch)
catch_discover_tests(${TARGET_MAIN_TESTS})