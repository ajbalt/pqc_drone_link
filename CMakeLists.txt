cmake_minimum_required(VERSION 3.28)
project(pqc_drone_link LANGUAGES C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# We set -Wall -Wextra -Werror on our own C code only; third-party code (if any is added
# later) must not be built with these flags.
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE -Wall -Wextra -Werror -Wmissing-prototypes)

# CMAKE_C_EXTENSIONS OFF (strict -std=c17), POSIX functions like clock_gettime and the socket APIs are hidden.
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Shared headers (errors.h); everything links this
add_library(pqc_common INTERFACE)
target_include_directories(pqc_common INTERFACE src)

# Randomness, secure_wipe and constant-time compare
add_library(pqc_crypto STATIC src/crypto/rng.c)
target_link_libraries(pqc_crypto PUBLIC pqc_common PRIVATE project_warnings)

add_library(pqc_metrics STATIC src/metrics/timer.c src/metrics/logger.c)
target_link_libraries(pqc_metrics PUBLIC pqc_common PRIVATE project_warnings)

enable_testing()

add_executable(test_rng tests/test_rng.c)
target_link_libraries(test_rng PRIVATE pqc_crypto project_warnings)
add_test(NAME test_rng COMMAND test_rng)

add_executable(test_metrics tests/test_metrics.c)
target_link_libraries(test_metrics PRIVATE pqc_metrics project_warnings)
add_test(NAME test_metrics COMMAND test_metrics)

# Documentation: optional, so the drone and other build machines don't need doxygen.
# Not part of the default build; generate with: cmake --build build --target docs
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
  set(DOXYGEN_PROJECT_NAME "PQC-1: Quantum-Secure Drone Link")
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)
  set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
  set(DOXYGEN_EXTRACT_STATIC YES)        # document static helpers in .c files too
  set(DOXYGEN_RECURSIVE YES)
  set(DOXYGEN_FILE_PATTERNS *.h *.c *.md)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE ${CMAKE_SOURCE_DIR}/README.md)
  set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
  set(DOXYGEN_GENERATE_LATEX NO)

  doxygen_add_docs(docs
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/docs
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/CONTRIBUTING.md
    COMMENT "Generating API documentation with Doxygen")
else()
  message(STATUS "Doxygen not found; 'docs' target unavailable")
endif()