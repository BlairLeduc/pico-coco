# Host tests (design.md §15). Every one of these builds src/core/ with the
# system compiler and no Pico SDK. One binary per area; exit 77 is a skip,
# and a skip is not a pass (§15.1).

set(PICO_COCO_HOST_TESTS
    test_skeleton
    test_mc6809_behaviour
    test_bench
)

foreach(t ${PICO_COCO_HOST_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE pico_coco_core)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${t} PRIVATE m)
    target_compile_options(${t} PRIVATE ${PICO_COCO_WARNINGS})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
# The bench's library brings the slow path, which the CPU tests define
# for themselves, so only test_bench links it.
target_link_libraries(test_bench PRIVATE pico_coco_bench)

# The CPU oracle (§7.4, §15.2): our core against XRoar's mc6809.c, from the
# pinned checkout tools/trace/build-xroar.sh makes. XRoar's files are
# compiled here as a tool and linked only into this test (D2, D8); none is
# in the tree. Without the checkout the test is built as a skip, so CTest
# says so rather than leaving it out.
set(PICO_COCO_XROAR_DIR ${PROJECT_SOURCE_DIR}/reference/xroar
    CACHE PATH "XRoar checkout at the pinned release (tools/trace/build-xroar.sh)")

if (EXISTS ${PICO_COCO_XROAR_DIR}/src/mc6809/mc6809.c)
    # XRoar's own warnings are not ours to fix: no -Werror on its files.
    add_library(xroar_mc6809 STATIC
        ${PICO_COCO_XROAR_DIR}/src/mc6809/mc6809.c
        ${PICO_COCO_XROAR_DIR}/portalib/delegate.c
        xroar_cpu.c
    )
    target_include_directories(xroar_mc6809 PRIVATE
        ${PICO_COCO_XROAR_DIR}
        ${PICO_COCO_XROAR_DIR}/src
        ${PICO_COCO_XROAR_DIR}/portalib
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(xroar_mc6809 PROPERTIES C_EXTENSIONS ON)

    add_executable(test_mc6809_oracle test_mc6809_oracle.c)
    target_link_libraries(test_mc6809_oracle PRIVATE pico_coco_core xroar_mc6809)
    target_include_directories(test_mc6809_oracle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_mc6809_oracle PRIVATE ${PICO_COCO_WARNINGS})
    # A Release build of the oracle runs in about a tenth of the time.
    target_compile_options(test_mc6809_oracle PRIVATE -O2)
else()
    message(STATUS "pico-coco: no XRoar at ${PICO_COCO_XROAR_DIR}; test_mc6809_oracle will skip "
                   "(tools/trace/build-xroar.sh --checkout-only)")
    add_executable(test_mc6809_oracle test_skip.c)
    target_include_directories(test_mc6809_oracle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_mc6809_oracle PRIVATE
        SKIP_REASON="no XRoar checkout at ${PICO_COCO_XROAR_DIR}: run tools/trace/build-xroar.sh --checkout-only")
endif()
add_test(NAME test_mc6809_oracle COMMAND test_mc6809_oracle)
set_tests_properties(test_mc6809_oracle PROPERTIES SKIP_RETURN_CODE 77 LABELS oracle)
