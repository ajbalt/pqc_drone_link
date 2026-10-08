cmake_minimum_required(VERSION 3.28) 
project(pqc_drone_link LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_SOURCE_DIR}/third_party)

# ML-KEM: use its own CMakeLists; it fetches sha3, randomshake, subtle itself.
# Its tests, benchmarks, examples, and fuzzers are all OFF by default.
add_subdirectory(${TP}/ml-kem)

add_library(mlkem_c STATIC src/crypto/mlkem_shim.cpp)
target_link_libraries(mlkem_c PRIVATE ml-kem)
target_include_directories(mlkem_c PUBLIC src/crypto)
target_compile_options(mlkem_c PRIVATE -fno-exceptions -fno-rtti)

# ASCON: its CMakeLists is a test harness, so compile just the folder we need
set(ASCON_IMPL ref CACHE STRING "ascon-c implementation folder (ref, opt64, armv7m, ...)")
set(ASCON_DIR ${TP}/ascon-c/crypto_aead_hash/asconaeadxof128/${ASCON_IMPL})
file(GLOB ASCON_SRCS ${ASCON_DIR}/*.c ${ASCON_DIR}/*.S)
add_library(ascon STATIC ${ASCON_SRCS})
target_include_directories(ascon PUBLIC ${ASCON_DIR} ${TP}/ascon-c/tests)

# We are setting -Wall -Wextra -Werror on our C code, but we don't want the sub modules
# causing errors as well
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror>)

#CMAKE_C_EXTENSIONS OFF (strict -std=c17), POSIX functions like clock_gettime and the socket APIs are hidden.
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Shared headers (errors.h); everything links this
add_library(pqc_common INTERFACE)
target_include_directories(pqc_common INTERFACE src)

add_library(pqc_crypto STATIC
  src/crypto/rng.c
  src/crypto/kem.c
  src/crypto/kem_none.c
  # week 2+: kem_mlkem.c kem_x25519.c kdf.c aead_ascon.c
)
target_link_libraries(pqc_crypto PUBLIC pqc_common PRIVATE project_warnings)
# week 2: target_link_libraries(pqc_crypto PRIVATE mlkem_c ascon)

add_library(pqc_transport STATIC src/transport/udp.c)
target_link_libraries(pqc_transport PUBLIC pqc_common PRIVATE project_warnings)

add_library(pqc_metrics STATIC src/metrics/timer.c src/metrics/logger.c)
target_link_libraries(pqc_metrics PUBLIC pqc_common PRIVATE project_warnings)

add_executable(drone src/app/drone.c src/app/telemetry_gen.c)
add_executable(ground_station src/app/ground_station.c)
foreach(t drone ground_station)
  target_link_libraries(${t} PRIVATE pqc_crypto pqc_transport pqc_metrics project_warnings)
endforeach()

enable_testing()
add_executable(test_kem tests/test_kem.c)
target_link_libraries(test_kem PRIVATE pqc_crypto project_warnings)
add_test(NAME test_kem COMMAND test_kem)

# Documentation: optional, so the drone and other build machines don't need doxygen.
# Not part of the default build; generate with: cmake --build build --target docs
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
  set(DOXYGEN_PROJECT_NAME "PQC-1: Quantum-Secure Drone Link")
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)
  set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
  set(DOXYGEN_EXTRACT_STATIC YES)        # document static helpers in .c files too
  set(DOXYGEN_RECURSIVE YES)
  set(DOXYGEN_FILE_PATTERNS *.h *.c *.cpp *.md)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE ${CMAKE_SOURCE_DIR}/README.md)
  set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
  set(DOXYGEN_GENERATE_LATEX NO)

  doxygen_add_docs(docs
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/docs
    ${CMAKE_SOURCE_DIR}/README.md
    COMMENT "Generating API documentation with Doxygen")
else()
  message(STATUS "Doxygen not found; 'docs' target unavailable")
endif()