# Acts like add_subdirectory(), but replaces Debug flags with RelWithDebugInfo flags.
# Use for unconditionally building performance-sensitive libraries in debug mode.
function(add_subdirectory_optimized)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

    add_subdirectory(${ARGV})
endfunction()
