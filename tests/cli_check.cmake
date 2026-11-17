# Runs the diffcore tool for one CTest test and checks what it did.
#
#   CLI            the executable; EMULATOR, if set, runs it (cross builds)
#   ARGS           its arguments, as a list
#   EXIT           the exit status it must return
#   EXPECT         a file whose bytes standard output must equal
#   MATCH          a regular expression standard output or standard error
#                  must match; \n and \t in it stand for newline and tab
#   COPY           a file to copy to RESULT before the run
#   RESULT         a file the run writes; with RESULT_EXPECT, a file whose
#                  bytes RESULT must equal afterwards

if(RESULT)
    get_filename_component(result_dir "${RESULT}" DIRECTORY)
    file(MAKE_DIRECTORY "${result_dir}")
    file(REMOVE "${RESULT}")
endif()
if(COPY)
    configure_file("${COPY}" "${RESULT}" COPYONLY)
endif()

set(command "${CLI}" ${ARGS})
if(EMULATOR)
    set(command ${EMULATOR} ${command})
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)

set(report "\ncommand: ${command}\nstdout:\n${out}\nstderr:\n${err}")
if(NOT "${status}" STREQUAL "${EXIT}")
    message(FATAL_ERROR "exit status ${status}, expected ${EXIT}${report}")
endif()

if(EXPECT)
    file(READ "${EXPECT}" expected)
    if(NOT "${out}" STREQUAL "${expected}")
        message(FATAL_ERROR "standard output differs from ${EXPECT}${report}")
    endif()
endif()

if(MATCH)
    string(REPLACE "\\n" "\n" pattern "${MATCH}")
    string(REPLACE "\\t" "\t" pattern "${pattern}")
    if(NOT "${out}" MATCHES "${pattern}" AND NOT "${err}" MATCHES "${pattern}")
        message(FATAL_ERROR "output does not match: ${MATCH}${report}")
    endif()
endif()

if(RESULT_EXPECT)
    if(NOT EXISTS "${RESULT}")
        message(FATAL_ERROR "${RESULT} was not written${report}")
    endif()
    file(READ "${RESULT}" actual)
    file(READ "${RESULT_EXPECT}" expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "${RESULT} differs from ${RESULT_EXPECT}${report}")
    endif()
endif()
