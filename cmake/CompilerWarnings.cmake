# Usage: zenith_set_warnings(<target>)
function(zenith_set_warnings target)
  if(MSVC)
    set(warnings /W4 /permissive-)
    if(ZENITH_WARNINGS_AS_ERRORS)
      list(APPEND warnings /WX)
    endif()
  else()
    set(warnings
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wformat=2)
    if(ZENITH_WARNINGS_AS_ERRORS)
      list(APPEND warnings -Werror)
    endif()
  endif()
  target_compile_options(${target} PRIVATE ${warnings})
endfunction()
