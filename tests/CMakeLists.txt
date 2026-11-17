set(DIFFCORE_TEST_SOURCES
    test_main.cpp
    test_apply.cpp
    test_golden.cpp
    test_hunks.cpp
    test_lines.cpp
    test_myers.cpp
    test_parse.cpp
    test_unified.cpp
)
# Checks that the public headers survive <windows.h> without NOMINMAX.
if(MINGW)
    list(APPEND DIFFCORE_TEST_SOURCES test_windows_h.cpp)
endif()

add_executable(diffcore_tests ${DIFFCORE_TEST_SOURCES})
target_include_directories(diffcore_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_include_directories(diffcore_tests PRIVATE ${PROJECT_SOURCE_DIR}/tools)
# doctest names test cases with __COUNTER__, which Clang 22 and later report
# under -Wpedantic as a C2y extension. Silence just that, for the tests only;
# it must come after -Wpedantic, hence a separate target linked last.
add_library(diffcore_test_flags INTERFACE)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wc2y-extensions DIFFCORE_HAVE_WC2Y_EXTENSIONS)
if(DIFFCORE_HAVE_WC2Y_EXTENSIONS)
    target_compile_options(diffcore_test_flags INTERFACE -Wno-c2y-extensions)
endif()
target_link_libraries(diffcore_tests PRIVATE diffcore diffcore_build_flags diffcore_test_flags)
target_compile_definitions(diffcore_tests PRIVATE
    DIFFCORE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DIFFCORE_PROJECT_VERSION="${PROJECT_VERSION}")
add_test(NAME diffcore_tests COMMAND diffcore_tests)

# Repository checks.
add_test(NAME no_std_byte
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${PROJECT_SOURCE_DIR}/tools/check_no_std_byte.cmake)

add_executable(check_vcxproj ${PROJECT_SOURCE_DIR}/tools/check_vcxproj.cpp)
target_link_libraries(check_vcxproj PRIVATE diffcore_build_flags)
add_test(NAME vcxproj_in_sync COMMAND check_vcxproj ${PROJECT_SOURCE_DIR})

if(TARGET diffcore_example)
    add_test(NAME example COMMAND diffcore_example
        ${PROJECT_SOURCE_DIR}/examples/checklist-v1.txt ${PROJECT_SOURCE_DIR}/examples/checklist-v2.txt)
    set_tests_properties(example PROPERTIES PASS_REGULAR_EXPRESSION
        "hunk 1 applied at line 5 \\(offset 1\\)\nhunk 2 applied at line 17 \\(offset 1\\)\nreverse apply restores the old text")
endif()

# Command-line tests: tests/cli_check.cmake runs the tool and checks its exit
# status and output.
if(TARGET diffcore_cli)
    set(D ${CMAKE_CURRENT_SOURCE_DIR}/data/cli)
    set(G ${CMAKE_CURRENT_SOURCE_DIR}/data/golden)
    set(OUT ${CMAKE_CURRENT_BINARY_DIR}/cli)
    function(diffcore_cli_test name)
        cmake_parse_arguments(T "" "EXIT;EXPECT;MATCH;RESULT;RESULT_EXPECT;COPY" "ARGS" ${ARGN})
        add_test(NAME cli_${name} COMMAND ${CMAKE_COMMAND}
            "-DCLI=$<TARGET_FILE:diffcore_cli>"
            "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
            "-DARGS=${T_ARGS}"
            "-DEXIT=${T_EXIT}"
            "-DEXPECT=${T_EXPECT}"
            "-DMATCH=${T_MATCH}"
            "-DRESULT=${T_RESULT}"
            "-DRESULT_EXPECT=${T_RESULT_EXPECT}"
            "-DCOPY=${T_COPY}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_check.cmake)
    endfunction()

    diffcore_cli_test(diff_same EXIT 0 EXPECT ${D}/empty.txt ARGS diff ${D}/old.txt ${D}/old.txt)
    diffcore_cli_test(diff_labels EXIT 1 EXPECT ${D}/old_new.diff
        ARGS diff --label a --label b ${D}/old.txt ${D}/new.txt)
    diffcore_cli_test(diff_context1 EXIT 1 EXPECT ${G}/u1_two_hunks.diff
        ARGS diff -U1 -L a -L b ${G}/u1_two_hunks.old ${G}/u1_two_hunks.new)
    diffcore_cli_test(diff_mtime_header EXIT 1
        MATCH "^--- [^\\n]*old\\.txt\\t[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]\\.[0-9]+ \\+0000\\n\\+\\+\\+ [^\\n]*new\\.txt\\t"
        ARGS diff ${D}/old.txt ${D}/new.txt)
    diffcore_cli_test(diff_missing_file EXIT 2 MATCH "no-such-file" ARGS diff ${D}/old.txt ${D}/no-such-file.txt)
    diffcore_cli_test(diff_bad_option EXIT 2 MATCH "unknown option" ARGS diff --frobnicate ${D}/old.txt ${D}/new.txt)
    diffcore_cli_test(diff_cost_limit EXIT 2 MATCH "exceeds the limit of 2 lines"
        ARGS diff --max-cost 2 ${D}/old.txt ${D}/new.txt)

    diffcore_cli_test(stat EXIT 1 MATCH "^3 lines added, 2 lines removed, 2 hunks\\n$"
        ARGS stat ${D}/old.txt ${D}/new.txt)

    diffcore_cli_test(patch_exact EXIT 0 MATCH "^patching file [^\\n]*\\n$"
        COPY ${D}/old.txt RESULT ${OUT}/exact.txt RESULT_EXPECT ${D}/new.txt
        ARGS patch ${OUT}/exact.txt ${D}/old_new.diff)
    diffcore_cli_test(patch_offset EXIT 0
        MATCH "^patching file [^\\n]*\\nHunk #1 succeeded at 3 \\(offset 2 lines\\)\\.\\nHunk #2 succeeded at 23 \\(offset 2 lines\\)\\.\\n$"
        COPY ${D}/shifted.txt RESULT ${OUT}/offset.txt RESULT_EXPECT ${D}/shifted_patched.txt
        ARGS patch ${OUT}/offset.txt ${D}/old_new.diff)
    diffcore_cli_test(patch_output_option EXIT 0 MATCH "\\(read from [^\\n]*old\\.txt\\)"
        RESULT ${OUT}/written.txt RESULT_EXPECT ${D}/new.txt
        ARGS patch -o ${OUT}/written.txt ${D}/old.txt ${D}/old_new.diff)
    diffcore_cli_test(patch_reverse EXIT 0 RESULT ${OUT}/reversed.txt RESULT_EXPECT ${D}/old.txt
        ARGS patch -R -o ${OUT}/reversed.txt ${D}/new.txt ${D}/old_new.diff)
    diffcore_cli_test(patch_failed EXIT 1
        MATCH "Hunk #2 FAILED at 21\\.\\n1 out of 2 hunks FAILED -- nothing written\\n$"
        COPY ${D}/conflict.txt RESULT ${OUT}/conflict.txt RESULT_EXPECT ${D}/conflict.txt
        ARGS patch ${OUT}/conflict.txt ${D}/old_new.diff)
    diffcore_cli_test(patch_dry_run EXIT 0 MATCH "^checking file "
        COPY ${D}/old.txt RESULT ${OUT}/dry.txt RESULT_EXPECT ${D}/old.txt
        ARGS patch --dry-run ${OUT}/dry.txt ${D}/old_new.diff)
    diffcore_cli_test(patch_malformed EXIT 2 MATCH "malformed\\.diff:4: unexpected line in hunk"
        ARGS patch ${D}/old.txt ${D}/malformed.diff)
    diffcore_cli_test(patch_garbage EXIT 2 MATCH "only garbage" ARGS patch ${D}/old.txt ${D}/old.txt)

    diffcore_cli_test(bench EXIT 0
        MATCH "^old: 2000 lines, new: [0-9]+ lines\\nedit distance D: [0-9]+ \\([0-9]+ deleted, [0-9]+ inserted\\), [0-9]+ hunks\\ndiff time: [0-9.]+ ms\\n$"
        ARGS bench --lines 2000 --change-rate 0.02 --seed 7)
    diffcore_cli_test(bench_needs_lines EXIT 2 MATCH "needs --lines" ARGS bench --change-rate 0.1)
    diffcore_cli_test(version EXIT 0 MATCH "^diffcore ${PROJECT_VERSION}\\n$" ARGS --version)
endif()
