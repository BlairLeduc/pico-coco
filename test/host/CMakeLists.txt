# Host tests (design.md §15). Every one of these builds src/core/ with the
# system compiler and no Pico SDK. One binary per area; exit 77 is a skip,
# and a skip is not a pass (§15.1).

set(PICO_COCO_HOST_TESTS
    test_skeleton
)

foreach(t ${PICO_COCO_HOST_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE pico_coco_core)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${t} PRIVATE ${PICO_COCO_WARNINGS})
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
