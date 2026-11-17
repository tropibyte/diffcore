cmake_minimum_required(VERSION 3.20)

project(diffcore
    VERSION 0.1.0
    DESCRIPTION "Line-based diff, unified diff output and patch apply"
    LANGUAGES CXX)

# PROJECT_IS_TOP_LEVEL needs CMake 3.21.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(DIFFCORE_IS_TOP_LEVEL ON)
else()
    set(DIFFCORE_IS_TOP_LEVEL OFF)
endif()

option(DIFFCORE_BUILD_TESTS "Build the unit tests" ${DIFFCORE_IS_TOP_LEVEL})
option(DIFFCORE_BUILD_CLI "Build the diffcore command-line tool" ${DIFFCORE_IS_TOP_LEVEL})
option(DIFFCORE_BUILD_EXAMPLES "Build the example program" ${DIFFCORE_IS_TOP_LEVEL})
option(DIFFCORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(DIFFCORE_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

# The library must also build inside Visual Studio solutions that compile
# everything as C++17, so stay on C++17 here.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(diffcore STATIC
    src/apply.cpp
    src/diff.cpp
    src/hunks.cpp
    src/lines.cpp
    src/myers.cpp
    src/patch.cpp
    src/tokens.cpp
    src/unified_format.cpp
    src/unified_parse.cpp
)
add_library(diffcore::diffcore ALIAS diffcore)

target_include_directories(diffcore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(diffcore PUBLIC cxx_std_17)

add_library(diffcore_build_flags INTERFACE)
if(MSVC)
    target_compile_options(diffcore_build_flags INTERFACE
        /W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor)
    # Projects that include the library build with std::byte removed.
    target_compile_definitions(diffcore_build_flags INTERFACE _HAS_STD_BYTE=0)
    if(DIFFCORE_WARNINGS_AS_ERRORS)
        target_compile_options(diffcore_build_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(diffcore_build_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor -Wold-style-cast)
    if(DIFFCORE_WARNINGS_AS_ERRORS)
        target_compile_options(diffcore_build_flags INTERFACE -Werror)
    endif()
    if(DIFFCORE_SANITIZE)
        target_compile_options(diffcore_build_flags INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(diffcore_build_flags INTERFACE -fsanitize=address,undefined)
    endif()
endif()
target_link_libraries(diffcore PRIVATE $<BUILD_INTERFACE:diffcore_build_flags>)

if(DIFFCORE_BUILD_CLI)
    add_executable(diffcore_cli tools/diffcore_cli.cpp)
    set_target_properties(diffcore_cli PROPERTIES OUTPUT_NAME diffcore)
    target_link_libraries(diffcore_cli PRIVATE diffcore diffcore_build_flags)
endif()

if(DIFFCORE_BUILD_EXAMPLES)
    add_executable(diffcore_example examples/diffcore_example.cpp)
    target_link_libraries(diffcore_example PRIVATE diffcore diffcore_build_flags)
endif()

if(DIFFCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS diffcore EXPORT diffcoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(DIFFCORE_BUILD_CLI)
    install(TARGETS diffcore_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/diffcore DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT diffcoreTargets NAMESPACE diffcore:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/diffcore)
