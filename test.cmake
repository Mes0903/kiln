function(_ep_write_gitclone_script
  script_filename
  source_dir
  git_EXECUTABLE
  git_repository
  git_tag
  git_remote_name
  init_submodules
  git_submodules_recurse
  git_submodules
  git_shallow
  git_progress
  git_config
  src_name
  work_dir
  gitclone_infofile
  gitclone_stampfile
  tls_version
  tls_verify
)

  if(Git_VERSION VERSION_LESS 2.20 OR
    2.21 VERSION_LESS_EQUAL Git_VERSION)
    set(git_clone_options "--no-checkout")
  else()
    set(git_clone_options)
  endif()
  if(git_shallow)
    if(NOT Git_VERSION VERSION_LESS 1.7.10)
      list(APPEND git_clone_options "--depth 1 --no-single-branch")
    else()
      list(APPEND git_clone_options "--depth 1")
    endif()
  endif()
  if(git_progress)
    list(APPEND git_clone_options --progress)
  endif()
  foreach(config IN LISTS git_config)
    list(APPEND git_clone_options --config \"${config}\")
  endforeach()
  if(NOT ${git_remote_name} STREQUAL "origin")
    list(APPEND git_clone_options --origin \"${git_remote_name}\")
  endif()

endfunction()



function(_ep_write_downloadfile_script
  script_filename
  REMOTE
  LOCAL
  timeout
  inactivity_timeout
  no_progress
  hash
  tls_version
  tls_verify
  tls_cainfo
  userpwd
  http_headers
  netrc
  netrc_file
)

  # REMOTE could contain special characters that parse as separate arguments.
  # Things like parentheses are legitimate characters in a URL, but would be
  # seen as the start of a new unquoted argument by the cmake language parser.
  # Avoid those special cases by preparing quoted strings for direct inclusion
  # in the foreach() call that iterates over the set of URLs in REMOTE.
  set(REMOTE "[====[${REMOTE}]====]")
  string(REPLACE ";" "]====] [====[" REMOTE "${REMOTE}")

endfunction()

function(_ep_add_script_commands script_var work_dir cmd)
  # There can be multiple COMMANDs, but we have to split those up to
  # one command per call to execute_process()
  string(CONCAT execute_process_cmd
    "execute_process(\n"
    "  WORKING_DIRECTORY \"${work_dir}\"\n"
    "  COMMAND_ERROR_IS_FATAL LAST\n"
  )
  cmake_language(GET_MESSAGE_LOG_LEVEL active_log_level)
  if(active_log_level MATCHES "VERBOSE|DEBUG|TRACE")
    string(APPEND execute_process_cmd "  COMMAND_ECHO STDOUT\n")
  endif()
  string(APPEND execute_process_cmd "  COMMAND ")

  string(APPEND ${script_var} "${execute_process_cmd}")

  foreach(cmd_arg IN LISTS cmd)
    if(cmd_arg STREQUAL "COMMAND")
      string(APPEND ${script_var} "\n)\n${execute_process_cmd}")
    else()
      if(_EP_LIST_SEPARATOR)
        string(REPLACE "${_EP_LIST_SEPARATOR}" "\\;" cmd_arg "${cmd_arg}")
      endif()
      foreach(dir IN LISTS location_tags)
        string(REPLACE "<${dir}>" "${_EP_${dir}}" cmd_arg "${cmd_arg}")
      endforeach()
      string(APPEND ${script_var} " [====[${cmd_arg}]====]")
    endif()
  endforeach()

  string(APPEND ${script_var} "\n)")
  set(${script_var} "${${script_var}}" PARENT_SCOPE)
endfunction()

