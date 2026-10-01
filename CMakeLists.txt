cmake_minimum_required(VERSION 3.28) 
project(pqc_dron_link LANGUAGE C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

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
target_include_directories(ascon PUBLIC ${ASCON_DIR})