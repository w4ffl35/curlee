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
