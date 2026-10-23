####################
# Project
get_filename_component(DIRECTORY_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
string(REPLACE " " "_" TARGET_MAIN ${DIRECTORY_NAME})
set(TARGET_MAIN_TESTS ${TARGET_MAIN}_test)

project(${TARGET_MAIN_TESTS} LANGUAGES CXX)

####################
# Sources & headers
aux_source_directory(. SRC_LIST)
file(GLOB HEADERS_LIST "*.h" "*.hpp")

####################
# Executable
enable_testing()

add_executable(${TARGET_MAIN_TESTS} ${SRC_LIST} ${HEADERS_LIST})

target_compile_features(${TARGET_MAIN_TESTS} PRIVATE cxx_std_23)
target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)
target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Boost::capy)

# enable_sanitizers(${TARGET_MAIN_TESTS})
# enable_thread_sanitizer(${TARGET_MAIN_TESTS})

include(CTest)
include(Catch)
catch_discover_tests(${TARGET_MAIN_TESTS})