# Fails if the library sources mention std::byte. Projects that use the
# library compile with _HAS_STD_BYTE=0, which removes std::byte, so the
# library must do without it; this catches uses on compilers other than MSVC.
#
#   cmake -DROOT=<source dir> -P tools/check_no_std_byte.cmake

if(NOT ROOT)
    message(FATAL_ERROR "set ROOT to the source directory")
endif()

file(GLOB_RECURSE sources
    "${ROOT}/include/*.hpp" "${ROOT}/include/*.h"
    "${ROOT}/src/*.cpp" "${ROOT}/src/*.hpp" "${ROOT}/src/*.h" "${ROOT}/src/*.inc")
list(LENGTH sources count)
if(count EQUAL 0)
    message(FATAL_ERROR "no sources found under ${ROOT}")
endif()

set(offenders "")
foreach(file IN LISTS sources)
    file(STRINGS "${file}" hits REGEX "std::byte")
    if(hits)
        list(APPEND offenders "${file}")
    endif()
endforeach()

if(offenders)
    list(JOIN offenders "\n  " text)
    message(FATAL_ERROR "std::byte is used in:\n  ${text}")
endif()
message(STATUS "checked ${count} files: no std::byte")
