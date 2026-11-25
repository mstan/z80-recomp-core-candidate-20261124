cmake_minimum_required(VERSION 3.16)
project(z80_recomp_core LANGUAGES C)

# Header-only state, host ABI, and verified instruction semantics. Generated
# code links no platform implementation from this repository: each consumer
# supplies its own memory, I/O, scheduler, and fallback hooks.
add_library(z80_recomp_core INTERFACE)
add_library(z80-recomp-core::core ALIAS z80_recomp_core)
target_include_directories(z80_recomp_core INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include")

include(CTest)
if(BUILD_TESTING)
    add_executable(z80_recomp_core_header_smoke tests/header_smoke.c)
    target_link_libraries(z80_recomp_core_header_smoke PRIVATE
        z80-recomp-core::core)
    add_test(NAME z80_recomp_core_header_smoke
             COMMAND z80_recomp_core_header_smoke)
endif()


add_library(z80_recomp_frontend STATIC src/z80_decoder.c src/z80_codegen.c)
add_library(z80-recomp-core::frontend ALIAS z80_recomp_frontend)
target_link_libraries(z80_recomp_frontend PUBLIC z80-recomp-core::core)
target_compile_features(z80_recomp_frontend PUBLIC c_std_11)
if(BUILD_TESTING)
 add_executable(z80_frontend_test tests/frontend_test.c)
 target_link_libraries(z80_frontend_test PRIVATE z80_recomp_frontend)
 add_test(NAME z80_frontend COMMAND z80_frontend_test)
 add_executable(z80_generate_native tests/generate_native.c)
 target_link_libraries(z80_generate_native PRIVATE z80_recomp_frontend)
 add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated_native.c"
   COMMAND z80_generate_native "${CMAKE_CURRENT_BINARY_DIR}/generated_native.c"
   DEPENDS z80_generate_native VERBATIM)
 add_executable(z80_native_execution tests/native_execution.c "${CMAKE_CURRENT_BINARY_DIR}/generated_native.c")
 target_link_libraries(z80_native_execution PRIVATE z80-recomp-core::core)
 add_test(NAME z80_native_execution COMMAND z80_native_execution)
endif()

if(BUILD_TESTING)
  add_executable(z80_ordered_read16_test tests/ordered_read16_test.c)
  target_include_directories(z80_ordered_read16_test PRIVATE include)
  add_test(NAME z80_ordered_read16 COMMAND z80_ordered_read16_test)
endif()
