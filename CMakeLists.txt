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

# Metrics
add_library(pqc_metrics STATIC src/metrics/timer.c src/metrics/logger.c)
target_link_libraries(pqc_metrics PUBLIC pqc_common PRIVATE project_warnings)

# Application wire format: RECORD and ACK messages
add_library(pqc_proto STATIC src/proto/wire.c)
target_link_libraries(pqc_proto PUBLIC pqc_common PRIVATE project_warnings)

add_executable(test_wire tests/test_wire.c)
target_link_libraries(test_wire PRIVATE pqc_proto project_warnings)
add_test(NAME test_wire COMMAND test_wire)

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

# Blocking IPv4 TCP helpers
add_library(pqc_net STATIC src/net/tcp.c)
target_link_libraries(pqc_net PUBLIC pqc_common PRIVATE project_warnings)

add_executable(test_tcp tests/test_tcp.c)
target_link_libraries(test_tcp PRIVATE pqc_net project_warnings)
add_test(NAME test_tcp COMMAND test_tcp)

# TLS link (OpenSSL 3.5+). Point CMake at the pinned build with
# -DOPENSSL_ROOT_DIR=$HOME/opt/openssl-3.5.9 (docs/build_openssl.md).
option(PQC_BUILD_TLS "Build the TLS link and its tests (needs OpenSSL 3.5+)" ON)
if(PQC_BUILD_TLS)
  find_package(OpenSSL 3.5 REQUIRED)
  add_library(pqc_tls STATIC src/tls/tls_link.c)
  target_link_libraries(pqc_tls PUBLIC pqc_common OpenSSL::SSL PRIVATE project_warnings)

  # Phase 1 programs and their end-to-end test (real processes over loopback)
  add_executable(tls_server pi/tls_server.c)
  target_link_libraries(tls_server PRIVATE pqc_tls pqc_net pqc_proto project_warnings)
  add_executable(tls_client pi/tls_client.c)
  target_link_libraries(tls_client PRIVATE pqc_tls pqc_net pqc_proto pqc_metrics project_warnings)
  add_test(NAME e2e_tls_link
           COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e_tls_link.sh $<TARGET_FILE:tls_server>
                   $<TARGET_FILE:tls_client> ${CMAKE_BINARY_DIR}/certs/main ${CMAKE_BINARY_DIR}/certs/other)
  set_tests_properties(e2e_tls_link PROPERTIES FIXTURES_REQUIRED tls_certs)

  # The tests need demo certificates (two CAs). Generate them with the same
  # openssl binary that the library came from.
  find_program(PQC_OPENSSL_BIN openssl HINTS "${OPENSSL_INCLUDE_DIR}/../bin"
               NO_DEFAULT_PATH REQUIRED)
  get_filename_component(PQC_OPENSSL_LIBDIR "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
  foreach(name main other)
    add_test(NAME tls_demo_certs_${name}
      COMMAND ${CMAKE_COMMAND} -E env LD_LIBRARY_PATH=${PQC_OPENSSL_LIBDIR}
              bash ${CMAKE_SOURCE_DIR}/scripts/make_demo_certs.sh
              ${PQC_OPENSSL_BIN} ${CMAKE_BINARY_DIR}/certs/${name})
    set_tests_properties(tls_demo_certs_${name} PROPERTIES FIXTURES_SETUP tls_certs)
  endforeach()

  add_executable(test_tls_link tests/test_tls_link.c)
  target_link_libraries(test_tls_link PRIVATE pqc_tls pqc_proto project_warnings)
  add_test(NAME test_tls_link
           COMMAND test_tls_link ${CMAKE_BINARY_DIR}/certs/main ${CMAKE_BINARY_DIR}/certs/other)
  set_tests_properties(test_tls_link PROPERTIES FIXTURES_REQUIRED tls_certs)

  # T10 handshake benchmark client, with a smoke test against real servers
  add_executable(bench_handshake experiments/bench_handshake.c)
  target_link_libraries(bench_handshake PRIVATE pqc_tls pqc_net pqc_metrics project_warnings)
  add_test(NAME e2e_bench
           COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e_bench.sh $<TARGET_FILE:tls_server>
                   $<TARGET_FILE:bench_handshake> ${CMAKE_SOURCE_DIR}/scripts/summarize_handshakes.py
                   ${CMAKE_BINARY_DIR}/certs/main)
  set_tests_properties(e2e_bench PROPERTIES FIXTURES_REQUIRED tls_certs)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME test_summarize COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tests/test_summarize.py)
endif()
