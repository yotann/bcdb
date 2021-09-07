set(REVISION_DESCRIPTION "" CACHE STRING "Extra string to be printed with --version")

function(get_revision_description _var)
  if(REVISION_DESCRIPTION)
    set(${_var} "${REVISION_DESCRIPTION}" PARENT_SCOPE)
  else()
    # NOTE: git_describe is automatically recomputed when HEAD changes, but
    # git_local_changes is never automatically recomputed. (The alternative
    # git_describe_working_tree is also never automatically recomputed.)
    # This seems okay for now.
    # An alternative that always recomputes everything:
    # https://cmake.org/pipermail/cmake/2010-July/038015.html
    include(GetGitRevisionDescription)
    git_describe(out --long)
    git_local_changes(out2)
    set(${_var} "${out}-${out2}" PARENT_SCOPE)
  endif()
endfunction()
