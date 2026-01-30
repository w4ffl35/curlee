cmake_minimum_required(VERSION 3.23)

project(curlee LANGUAGES CXX)

# Curlee is Linux-first for now.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(WARNING "Curlee currently targets Linux as the primary platform. Your platform is: ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(CURLEE_USE_SYSTEM_Z3 "Use system-installed Z3 if available; otherwise fetch" ON)
option(CURLEE_ENABLE_FUZZING "Build fuzz targets (libFuzzer when using Clang)" OFF)

if(CURLEE_USE_SYSTEM_Z3)
  find_package(Z3 QUIET)
endif()

if(NOT Z3_FOUND)
  include(FetchContent)
  set(Z3_BUILD_LIBZ3_SHARED OFF CACHE BOOL "" FORCE)
  set(Z3_BUILD_EXECUTABLE OFF CACHE BOOL "" FORCE)
  set(Z3_BUILD_TEST_EXECUTABLES OFF CACHE BOOL "" FORCE)
  set(Z3_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
  set(Z3_BUILD_DOTNET_BINDINGS OFF CACHE BOOL "" FORCE)
  set(Z3_BUILD_JAVA_BINDINGS OFF CACHE BOOL "" FORCE)
  set(Z3_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    z3
    GIT_REPOSITORY https://github.com/Z3Prover/z3.git
    GIT_TAG z3-4.12.5
  )
  FetchContent_MakeAvailable(z3)

  if(TARGET libz3)
    add_library(Z3::Z3 ALIAS libz3)
  elseif(TARGET z3)
    add_library(Z3::Z3 ALIAS z3)
  else()
    message(FATAL_ERROR "Z3 target not found after FetchContent")
  endif()
endif()

if(NOT TARGET Z3::Z3)
  message(FATAL_ERROR "Z3 not found: install libz3-dev or set CURLEE_USE_SYSTEM_Z3=OFF to fetch")
endif()

# Keep warnings sensible by default. You can tighten these later.
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Minimal placeholder target so the build system is usable immediately.
# (You can later rename this to a richer CLI target.)
add_executable(curlee
  src/main.cpp
  src/cli/cli.cpp
  src/bundle/bundle.cpp
  src/compiler/emitter.cpp
  src/diag/render.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
  src/types/type_check.cpp
  src/verification/checker.cpp
  src/verification/predicate_lowering.cpp
  src/verification/solver.cpp
  src/vm/vm.cpp
)

target_include_directories(curlee PRIVATE include)
target_link_libraries(curlee PRIVATE Z3::Z3)

add_executable(curlee_lsp
  src/lsp/lsp.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
  src/types/type_check.cpp
)
target_include_directories(curlee_lsp PRIVATE include)

enable_testing()

add_test(NAME curlee_help COMMAND curlee --help)

add_test(NAME curlee_no_args COMMAND curlee)
set_tests_properties(curlee_no_args PROPERTIES WILL_FAIL TRUE)

add_test(NAME curlee_missing_file COMMAND curlee lex ${CMAKE_SOURCE_DIR}/tests/fixtures/does_not_exist.cur)
set_tests_properties(curlee_missing_file PROPERTIES WILL_FAIL TRUE)

add_test(NAME curlee_lex_reads_file COMMAND curlee lex ${CMAKE_SOURCE_DIR}/tests/fixtures/hello.curlee)

add_executable(curlee_line_map_tests
  tests/line_map_tests.cpp
  src/source/line_map.cpp
)
target_include_directories(curlee_line_map_tests PRIVATE include)

add_test(NAME curlee_line_map_tests COMMAND curlee_line_map_tests)

add_executable(curlee_diagnostic_golden_tests
  tests/diagnostic_golden_tests.cpp
  src/diag/render.cpp
  src/source/line_map.cpp
)
target_include_directories(curlee_diagnostic_golden_tests PRIVATE include)

add_test(
  NAME curlee_diagnostic_golden_tests
  COMMAND curlee_diagnostic_golden_tests ${CMAKE_SOURCE_DIR}/tests/diagnostics
)

add_executable(curlee_cli_diagnostics_golden_tests
  tests/cli_diagnostics_golden_tests.cpp
  src/bundle/bundle.cpp
  src/cli/cli.cpp
  src/compiler/emitter.cpp
  src/diag/render.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
  src/types/type_check.cpp
  src/verification/checker.cpp
  src/verification/predicate_lowering.cpp
  src/verification/solver.cpp
  src/vm/vm.cpp
)
target_include_directories(curlee_cli_diagnostics_golden_tests PRIVATE include)
target_link_libraries(curlee_cli_diagnostics_golden_tests PRIVATE Z3::Z3)

add_executable(curlee_python_runner_fake_error
  tests/python_runner_fake_error.cpp
)

add_executable(curlee_python_runner_fake_hang
  tests/python_runner_fake_hang.cpp
)

add_executable(curlee_python_runner_fake_spam
  tests/python_runner_fake_spam.cpp
)

add_executable(curlee_python_runner_fake_env_check
  tests/python_runner_fake_env_check.cpp
)

add_test(
  NAME curlee_cli_diagnostics_golden_tests
  COMMAND curlee_cli_diagnostics_golden_tests ${CMAKE_SOURCE_DIR}/tests/cli_diagnostics $<TARGET_FILE:curlee_python_runner_fake_error> $<TARGET_FILE:curlee_python_runner_fake_hang> $<TARGET_FILE:curlee_python_runner_fake_spam> $<TARGET_FILE:curlee_python_runner_fake_env_check>
)
set_tests_properties(curlee_cli_diagnostics_golden_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(curlee_lexer_tests
  tests/lexer_tests.cpp
  src/lexer/lexer.cpp
)
target_include_directories(curlee_lexer_tests PRIVATE include)

add_test(NAME curlee_lexer_tests COMMAND curlee_lexer_tests)

if(CURLEE_ENABLE_FUZZING)
  add_executable(curlee_lexer_fuzz
    tests/lexer_fuzz.cpp
    src/lexer/lexer.cpp
  )
  target_include_directories(curlee_lexer_fuzz PRIVATE include)

  add_executable(curlee_parser_fuzz
    tests/parser_fuzz.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
  )
  target_include_directories(curlee_parser_fuzz PRIVATE include)

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(curlee_lexer_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(curlee_lexer_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

    target_compile_options(curlee_parser_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(curlee_parser_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

    add_test(
      NAME curlee_lexer_fuzz_smoke
      COMMAND curlee_lexer_fuzz -seed=1 -runs=200 -max_len=4096
    )
    set_tests_properties(curlee_lexer_fuzz_smoke PROPERTIES TIMEOUT 20)

    add_test(
      NAME curlee_parser_fuzz_smoke
      COMMAND curlee_parser_fuzz -seed=1 -runs=200 -max_len=4096
    )
    set_tests_properties(curlee_parser_fuzz_smoke PROPERTIES TIMEOUT 20)
  else()
    target_compile_definitions(curlee_lexer_fuzz PRIVATE CURLEE_FUZZER_STANDALONE)
    target_compile_definitions(curlee_parser_fuzz PRIVATE CURLEE_FUZZER_STANDALONE)
  endif()
endif()

add_executable(curlee_parser_tests
  tests/parser_tests.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
)
target_include_directories(curlee_parser_tests PRIVATE include)

add_test(NAME curlee_parser_tests COMMAND curlee_parser_tests)

add_executable(curlee_lsp_hover_golden_tests
  tests/lsp_hover_golden_tests.cpp
)

add_test(
  NAME curlee_lsp_hover_golden_tests
  COMMAND curlee_lsp_hover_golden_tests ${CMAKE_SOURCE_DIR}/tests/lsp_hover $<TARGET_FILE:curlee_lsp>
)
set_tests_properties(curlee_lsp_hover_golden_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(curlee_fuzz_regression_tests
  tests/fuzz_regression_tests.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
)
target_include_directories(curlee_fuzz_regression_tests PRIVATE include)

add_test(
  NAME curlee_fuzz_regression_tests
  COMMAND curlee_fuzz_regression_tests ${CMAKE_SOURCE_DIR}/tests/fuzz_regressions
)

add_executable(curlee_resolver_tests
  tests/resolver_tests.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/source_file.cpp
)
target_include_directories(curlee_resolver_tests PRIVATE include)

add_test(NAME curlee_resolver_tests COMMAND curlee_resolver_tests)

add_executable(curlee_types_tests
  tests/types_tests.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/types/type_check.cpp
)
target_include_directories(curlee_types_tests PRIVATE include)

add_test(NAME curlee_types_tests COMMAND curlee_types_tests)

add_executable(curlee_training_data_tests
  tests/training_data_tests.cpp
)

add_test(
  NAME curlee_training_data_tests
  COMMAND curlee_training_data_tests ${CMAKE_SOURCE_DIR}/scripts/generate_correct_samples.py ${CMAKE_BINARY_DIR}
)

add_executable(curlee_cli_fmt_tests
  tests/cli_fmt_tests.cpp
  src/bundle/bundle.cpp
  src/cli/cli.cpp
  src/compiler/emitter.cpp
  src/diag/render.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
  src/types/type_check.cpp
  src/verification/checker.cpp
  src/verification/predicate_lowering.cpp
  src/verification/solver.cpp
  src/vm/vm.cpp
)
target_include_directories(curlee_cli_fmt_tests PRIVATE include)
target_link_libraries(curlee_cli_fmt_tests PRIVATE Z3::Z3)

add_test(NAME curlee_cli_fmt_tests COMMAND curlee_cli_fmt_tests)

add_executable(curlee_cli_bundle_tests
  tests/cli_bundle_tests.cpp
  src/bundle/bundle.cpp
  src/cli/cli.cpp
  src/compiler/emitter.cpp
  src/diag/render.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
  src/types/type_check.cpp
  src/verification/checker.cpp
  src/verification/predicate_lowering.cpp
  src/verification/solver.cpp
  src/vm/vm.cpp
)
target_include_directories(curlee_cli_bundle_tests PRIVATE include)
target_link_libraries(curlee_cli_bundle_tests PRIVATE Z3::Z3)

add_test(NAME curlee_cli_bundle_tests COMMAND curlee_cli_bundle_tests)

add_executable(curlee_vm_tests
  tests/vm_tests.cpp
  src/vm/vm.cpp
)
target_include_directories(curlee_vm_tests PRIVATE include)

add_test(NAME curlee_vm_tests COMMAND curlee_vm_tests)

add_executable(curlee_bundle_tests
  tests/bundle_tests.cpp
  src/bundle/bundle.cpp
)
target_include_directories(curlee_bundle_tests PRIVATE include)

add_test(NAME curlee_bundle_tests COMMAND curlee_bundle_tests)

add_executable(curlee_interop_tests
  tests/interop_tests.cpp
  src/interop/python_ffi.cpp
)
target_include_directories(curlee_interop_tests PRIVATE include)

add_test(NAME curlee_interop_tests COMMAND curlee_interop_tests)

add_executable(curlee_python_runner
  src/interop/python_runner_main.cpp
)

add_executable(curlee_python_runner_tests
  tests/python_runner_tests.cpp
)

add_test(
  NAME curlee_python_runner_tests
  COMMAND curlee_python_runner_tests $<TARGET_FILE:curlee_python_runner>
)

add_executable(curlee_e2e_tests
  tests/e2e_tests.cpp
  src/compiler/emitter.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
  src/types/type_check.cpp
  src/verification/checker.cpp
  src/verification/predicate_lowering.cpp
  src/verification/solver.cpp
  src/vm/vm.cpp
)
target_include_directories(curlee_e2e_tests PRIVATE include)
target_link_libraries(curlee_e2e_tests PRIVATE Z3::Z3)

add_test(
  NAME curlee_e2e_tests
  COMMAND curlee_e2e_tests ${CMAKE_SOURCE_DIR}/tests/run ${CMAKE_SOURCE_DIR}/tests/correct_samples
)
set_tests_properties(curlee_e2e_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(curlee_compiler_tests
  tests/compiler_tests.cpp
  src/compiler/emitter.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/vm/vm.cpp
)
target_include_directories(curlee_compiler_tests PRIVATE include)

add_test(NAME curlee_compiler_tests COMMAND curlee_compiler_tests)

add_executable(curlee_tensor_ir_tests
  tests/tensor_ir_tests.cpp
  src/compiler/tensor_ir.cpp
)
target_include_directories(curlee_tensor_ir_tests PRIVATE include)

add_test(NAME curlee_tensor_ir_tests COMMAND curlee_tensor_ir_tests)

add_executable(curlee_verification_tests
  tests/verification_solver_tests.cpp
  src/verification/solver.cpp
)
target_include_directories(curlee_verification_tests PRIVATE include)
target_link_libraries(curlee_verification_tests PRIVATE Z3::Z3)

add_test(NAME curlee_verification_tests COMMAND curlee_verification_tests)

add_executable(curlee_verification_predicate_lowering_tests
  tests/verification_predicate_lowering_tests.cpp
  src/verification/predicate_lowering.cpp
  src/verification/solver.cpp
)
target_include_directories(curlee_verification_predicate_lowering_tests PRIVATE include)
target_link_libraries(curlee_verification_predicate_lowering_tests PRIVATE Z3::Z3)

add_test(
  NAME curlee_verification_predicate_lowering_tests
  COMMAND curlee_verification_predicate_lowering_tests
)
