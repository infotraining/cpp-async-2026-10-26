cmake_minimum_required(VERSION 3.16)
project(baremetal_sr_linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(baremetal_sr_linux src/main.cpp)
target_compile_options(baremetal_sr_linux PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
include(FetchContent)
FetchContent_Declare(
    async
    GIT_REPOSITORY https://github.com/intel/cpp-baremetal-senders-and-receivers.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(async)

add_executable(baremetal_sr_intel src/main_intel.cpp)
target_compile_features(baremetal_sr_intel PRIVATE cxx_std_23)
target_link_libraries(baremetal_sr_intel PRIVATE async Threads::Threads)
