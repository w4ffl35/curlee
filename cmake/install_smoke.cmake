if(NOT DEFINED curlee_build_dir)
  message(FATAL_ERROR "curlee_build_dir not set")
endif()

if(NOT DEFINED curlee_bindir)
  set(curlee_bindir "bin")
endif()

set(prefix "${curlee_build_dir}/install-smoke-prefix")
file(REMOVE_RECURSE "${prefix}")
file(MAKE_DIRECTORY "${prefix}")

set(build_cmd "${CMAKE_COMMAND}" --build "${curlee_build_dir}" --target curlee)
if(DEFINED curlee_config AND NOT "${curlee_config}" STREQUAL "")
  list(APPEND build_cmd --config "${curlee_config}")
endif()

execute_process(
  COMMAND ${build_cmd}
  RESULT_VARIABLE build_rc
  OUTPUT_VARIABLE build_out
  ERROR_VARIABLE build_err
)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR "build failed (rc=${build_rc})\nstdout:\n${build_out}\nstderr:\n${build_err}")
endif()

set(install_cmd "${CMAKE_COMMAND}" --install "${curlee_build_dir}" --prefix "${prefix}")
if(DEFINED curlee_config AND NOT "${curlee_config}" STREQUAL "")
  list(APPEND install_cmd --config "${curlee_config}")
endif()

execute_process(
  COMMAND ${install_cmd}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "install failed (rc=${rc})\nstdout:\n${out}\nstderr:\n${err}")
endif()

set(curlee_bin "${prefix}/${curlee_bindir}/curlee")
if(NOT EXISTS "${curlee_bin}")
  message(FATAL_ERROR "expected installed binary at: ${curlee_bin}")
endif()

execute_process(
  COMMAND "${curlee_bin}" --help
  RESULT_VARIABLE help_rc
  OUTPUT_VARIABLE help_out
  ERROR_VARIABLE help_err
)
if(NOT help_rc EQUAL 0)
  message(FATAL_ERROR "installed curlee --help failed (rc=${help_rc})\nstdout:\n${help_out}\nstderr:\n${help_err}")
endif()
