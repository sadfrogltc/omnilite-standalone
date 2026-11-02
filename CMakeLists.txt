cmake_minimum_required(VERSION 3.22)
project(omnilite-standalone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(OMNILITE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(OMNILITE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(EVENT REQUIRED IMPORTED_TARGET libevent)
pkg_check_modules(EVENT_PTHREADS REQUIRED IMPORTED_TARGET libevent_pthreads)
find_package(Boost 1.64 REQUIRED CONFIG COMPONENTS unit_test_framework)

set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/core)

# --- libsecp256k1, configured like Litecoin Core's build on 64-bit Linux ---
add_library(secp256k1 STATIC ${VENDOR}/secp256k1/src/secp256k1.c)
target_include_directories(secp256k1
  SYSTEM PUBLIC ${VENDOR}/secp256k1/include
  PRIVATE ${VENDOR}/secp256k1 ${VENDOR}/secp256k1/src)
target_compile_definitions(secp256k1 PRIVATE
  USE_NUM_NONE=1 USE_FIELD_INV_BUILTIN=1 USE_SCALAR_INV_BUILTIN=1
  USE_FIELD_5X52=1 USE_SCALAR_4X64=1 HAVE___INT128=1
  ENABLE_MODULE_RECOVERY=1)
target_compile_options(secp256k1 PRIVATE -w)

# --- UniValue ---
add_library(univalue STATIC
  ${VENDOR}/univalue/lib/univalue.cpp
  ${VENDOR}/univalue/lib/univalue_get.cpp
  ${VENDOR}/univalue/lib/univalue_read.cpp
  ${VENDOR}/univalue/lib/univalue_write.cpp)
target_include_directories(univalue SYSTEM PUBLIC ${VENDOR}/univalue/include)
target_compile_options(univalue PRIVATE -w)

# --- The parts of Litecoin Core the indexer builds on ---
add_library(litecoin_core STATIC
  ${VENDOR}/arith_uint256.cpp
  ${VENDOR}/base58.cpp
  ${VENDOR}/bech32.cpp
  ${VENDOR}/crypto/hmac_sha512.cpp
  ${VENDOR}/crypto/ripemd160.cpp
  ${VENDOR}/crypto/sha256.cpp
  ${VENDOR}/crypto/sha512.cpp
  ${VENDOR}/hash.cpp
  ${VENDOR}/key_io.cpp
  ${VENDOR}/primitives/transaction.cpp
  ${VENDOR}/pubkey.cpp
  ${VENDOR}/script/script.cpp
  ${VENDOR}/script/standard.cpp
  ${VENDOR}/support/cleanse.cpp
  ${VENDOR}/uint256.cpp
  ${VENDOR}/util/strencodings.cpp
  src/chainparams.cpp)
# src/ comes first: its chainparams.h replaces Litecoin Core's.
target_include_directories(litecoin_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# SYSTEM: warnings in vendored headers are not ours to fix.
target_include_directories(litecoin_core SYSTEM PUBLIC ${VENDOR})
# What configure detects for Litecoin Core on glibc.
target_compile_definitions(litecoin_core PUBLIC HAVE_ENDIAN_H=1 HAVE_BYTESWAP_H=1
  HAVE_DECL_BSWAP_16=1 HAVE_DECL_BSWAP_32=1 HAVE_DECL_BSWAP_64=1)
target_link_libraries(litecoin_core PUBLIC secp256k1 univalue Boost::headers)
target_compile_options(litecoin_core PRIVATE -w)

# --- OmniLite Standalone ---
add_library(omnilite STATIC
  src/config.cpp
  src/rpcclient.cpp)
target_include_directories(omnilite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(omnilite PUBLIC litecoin_core PkgConfig::EVENT)
target_compile_options(omnilite PRIVATE -Wall -Wextra)

add_executable(omnilite-standalone src/main.cpp)
target_link_libraries(omnilite-standalone PRIVATE omnilite)
target_compile_options(omnilite-standalone PRIVATE -Wall -Wextra)

# --- Tests ---
enable_testing()
add_executable(test_omnilite
  test/main.cpp
  test/config_tests.cpp
  test/core_tests.cpp
  test/rpcclient_tests.cpp)
target_link_libraries(test_omnilite PRIVATE omnilite Boost::unit_test_framework PkgConfig::EVENT_PTHREADS)
target_compile_options(test_omnilite PRIVATE -Wall -Wextra)
add_test(NAME unit COMMAND test_omnilite)
