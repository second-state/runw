# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.9.4)
project(RunW)

find_program(GIT_CMD git)
execute_process(COMMAND
  ${GIT_CMD} describe --match "*[0-9].[0-9]*" --tag
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE CPACK_PACKAGE_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FindPkgConfig)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Boost REQUIRED)
find_package(Filesystem REQUIRED Final Experimental)
find_package(Threads REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

if(CMAKE_BUILD_TYPE STREQUAL Release OR CMAKE_BUILD_TYPE STREQUAL RelWithDebInfo)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_ENABLE_EXPORTS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_SKIP_RPATH ON)

option(BUILD_TESTS "Generate build targets for the ssvm unit tests." OFF)
option(BUILD_COVERAGE "Generate coverage report. Require BUILD_TESTS." OFF)
set(BUILD_PACKAGE "TXZ;DEB;RPM" CACHE STRING "Package generate types")

if(BUILD_COVERAGE)
  include(CodeCoverage)
  append_coverage_compiler_flags()
endif()

string(TOUPPER "${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME}" CMAKE_INSTALL_DEFAULT_COMPONENT_NAME_UPCASE)
set(CPACK_PACKAGE_VENDOR Second State LLC)
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGE_CONTACT "Shen-Ta Hsieh <beststeve@secondstate.io>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OCI Runtime for WebAssembly")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_ARCHIVE_${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME_UPCASE}_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_MAIN_COMPONENT ${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME})
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME_UPCASE}_PACKAGE_NAME runw)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_COMPRESSION_TYPE xz)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/second-state/runw/")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEP ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS libllvm10)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_GENERATOR "${BUILD_PACKAGE}")
set(CPACK_PACKAGE_DESCRIPTION "runw is a high performance, extensible, and hardware optimized WebAssembly Virtual Machine for running OCI WebAssembly images.")

set(SIMDJSON_BUILD_STATIC ON)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.10.0
)
FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG        v0.7.1
)
FetchContent_Declare(
  ssvm
  GIT_REPOSITORY https://github.com/second-state/SSVM.git
  GIT_TAG        hydai/runc-wasi-fs
)

FetchContent_MakeAvailable(simdjson ssvm)

include(GNUInstallDirs)

if(BUILD_TESTS)
  FetchContent_MakeAvailable(googletest)
  include(CTest)
  add_subdirectory(test)
endif()

add_subdirectory(include)
add_subdirectory(src)

include(CPack)
include(CPackComponent)
