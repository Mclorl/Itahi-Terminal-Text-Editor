# itahi_set_warnings(<target>): the warnings used for our own code (never for
# dependencies). ITAHI_WARNINGS_AS_ERRORS turns them into errors.
function(itahi_set_warnings target)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wshadow>
    $<$<BOOL:${ITAHI_WARNINGS_AS_ERRORS}>:-Werror>)
endfunction()
