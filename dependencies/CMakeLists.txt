########################################################################
# Dependencies

Include(FetchContent)

########################################################################
# Catch2
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.16.0 # or a later release
)
FetchContent_MakeAvailable(Catch2)

include(CTest)
include(Catch)

# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

########################################################################
# stdexec
set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
  GIT_TAG        6d7ad689f4d4831c5136e4abe1c601f9a3b64e43
)
FetchContent_MakeAvailable(stdexec)
# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE STDEXEC::stdexec)

########################################################################
# capy
set(BOOST_CAPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BOOST_CAPY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BOOST_CAPY_BUILD_BENCH OFF CACHE BOOL "" FORCE)

FetchContent_Declare(capy
    GIT_REPOSITORY https://github.com/cppalliance/capy.git
    GIT_TAG develop
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(capy)

# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Boost::capy)

########################################################################
# corosio
set(BOOST_COROSIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BOOST_COROSIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BOOST_COROSIO_BUILD_BENCH OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(corosio
        GIT_REPOSITORY https://github.com/cppalliance/corosio.git
        GIT_TAG develop
        GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(corosio)

# later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE Boost::corosio)

# # if unix and not apple, then add liburing
# if(UNIX AND NOT APPLE)
#   # liburing
#   find_package(PkgConfig)
#   pkg_check_modules(liburing REQUIRED IMPORTED_TARGET GLOBAL liburing>=2.0)
#   # later add: target_link_libraries(${TARGET_MAIN_TESTS} PRIVATE PkgConfig::liburing)
# endif()