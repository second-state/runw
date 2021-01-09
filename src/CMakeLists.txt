# SPDX-License-Identifier: Apache-2.0

add_executable(runw
  bundle.cpp
  cgroup.cpp
  runw.cpp
  sdbus.cpp
  state.cpp
)

target_compile_options(runw
  PUBLIC
  ${SYSTEMD_CFLAGS}
  -Wall
  -Wextra
)

target_link_options(runw
  PUBLIC
  ${SYSTEMD_LDFLAGS}
)

target_include_directories(runw
  PUBLIC
  ${SYSTEMD_INCLUDE_DIRS}
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(runw
  PUBLIC
  ${SYSTEMD_LIBRARIES}
  ssvmCommon
  ssvmVM
  ssvmAOT
  simdjson
)

target_link_directories(runw
  PUBLIC
  ${SYSTEMD_LIBRARY_DIRS}
)
