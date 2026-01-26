cmake_minimum_required(VERSION 3.23)

project(curlee LANGUAGES CXX)

# Curlee is Linux-first for now.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(WARNING "Curlee currently targets Linux as the primary platform. Your platform is: ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  src/diag/render.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
)

target_include_directories(curlee PRIVATE include)

enable_testing()

add_test(NAME curlee_help COMMAND curlee --help)

add_test(NAME curlee_no_args COMMAND curlee)
set_tests_properties(curlee_no_args PROPERTIES WILL_FAIL TRUE)

add_test(NAME curlee_missing_file COMMAND curlee lex ${CMAKE_SOURCE_DIR}/tests/fixtures/does_not_exist.cur)
set_tests_properties(curlee_missing_file PROPERTIES WILL_FAIL TRUE)

add_test(NAME curlee_lex_reads_file COMMAND curlee lex ${CMAKE_SOURCE_DIR}/tests/fixtures/hello.cur)

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
  src/cli/cli.cpp
  src/diag/render.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/source/line_map.cpp
  src/source/source_file.cpp
)
target_include_directories(curlee_cli_diagnostics_golden_tests PRIVATE include)

add_test(
  NAME curlee_cli_diagnostics_golden_tests
  COMMAND curlee_cli_diagnostics_golden_tests ${CMAKE_SOURCE_DIR}/tests/cli_diagnostics
)
set_tests_properties(curlee_cli_diagnostics_golden_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(curlee_lexer_tests
  tests/lexer_tests.cpp
  src/lexer/lexer.cpp
)
target_include_directories(curlee_lexer_tests PRIVATE include)

add_test(NAME curlee_lexer_tests COMMAND curlee_lexer_tests)

add_executable(curlee_parser_tests
  tests/parser_tests.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
)
target_include_directories(curlee_parser_tests PRIVATE include)

add_test(NAME curlee_parser_tests COMMAND curlee_parser_tests)

add_executable(curlee_resolver_tests
  tests/resolver_tests.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/resolver/resolver.cpp
)
target_include_directories(curlee_resolver_tests PRIVATE include)

add_test(NAME curlee_resolver_tests COMMAND curlee_resolver_tests)
