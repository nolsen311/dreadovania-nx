# SPDX-License-Identifier: GPL-3.0-or-later
# Host build of the Switch-independent core, for unit tests (ADR 0004).
cmake_minimum_required(VERSION 3.20)
project(dreadovania_core_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION DREADOVANIA_VERSION LIMIT_COUNT 1)

add_library(dreadovania_core STATIC ../core/src/paths.cpp ../core/src/json.cpp ../core/src/api.cpp ../core/src/sha256.cpp ../core/src/install.cpp)
target_include_directories(dreadovania_core PUBLIC ../core/include)
target_compile_definitions(dreadovania_core PUBLIC DREADOVANIA_VERSION="${DREADOVANIA_VERSION}")

set(WARNINGS -Wall -Wextra -Werror -Wpedantic -Wconversion)
set(SANITIZE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
target_compile_options(dreadovania_core PRIVATE ${WARNINGS} ${SANITIZE})

add_executable(core_tests main.cpp test_paths.cpp test_json.cpp test_api.cpp test_install.cpp
               test_installer.cpp test_jobs.cpp ../source/installer.cpp ../source/jobs.cpp)
# source/installer.cpp and source/jobs.cpp with tests/fake/http.hpp standing in for the network.
target_include_directories(core_tests PRIVATE fake ../source)
target_link_libraries(core_tests PRIVATE dreadovania_core)
target_compile_options(core_tests PRIVATE ${WARNINGS} ${SANITIZE})
target_compile_definitions(core_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_options(core_tests PRIVATE ${SANITIZE})

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
