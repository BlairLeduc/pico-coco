# Host tests (design.md §15). Every one of these builds src/core/ with the
# system compiler and no Pico SDK. One binary per area; exit 77 is a skip,
# and a skip is not a pass (§15.1).

set(PICO_COCO_HOST_TESTS
    test_skeleton
    test_mc6809_behaviour
    test_bench
    test_mc6821
    test_machine
    test_mc6847
    test_video
    test_present
    test_keymap
    test_status
    test_textpage
    test_audio
    test_settings
    test_snapshot
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
# The tests that run the real ROMs share the harness (§15.3), and find
# the images in roms/ unless PICO_COCO_ROMS says otherwise.
foreach(t test_boot test_romset test_screen test_keyboard test_audio_rom test_tape test_cart
          test_snapshot_rom test_soak)
    add_executable(${t} ${t}.c guest.c frame_ppm.c)
    target_link_libraries(${t} PRIVATE pico_coco_core m)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${t} PRIVATE ${PICO_COCO_WARNINGS})
    target_compile_definitions(${t} PRIVATE
        PICO_COCO_DEFAULT_ROMS="${PROJECT_SOURCE_DIR}/roms"
        PICO_COCO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES SKIP_RETURN_CODE 77 LABELS rom)
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

# The VDG oracle (§9, §15.2): the snapshot and the renderer against XRoar's
# MC6847 and MC6883 from the same checkout, linked only into this test.
if (EXISTS ${PICO_COCO_XROAR_DIR}/src/mc6847/mc6847.c)
    add_library(xroar_vdg STATIC
        ${PICO_COCO_XROAR_DIR}/src/mc6847/mc6847.c
        ${PICO_COCO_XROAR_DIR}/src/mc6847/font-6847.c
        ${PICO_COCO_XROAR_DIR}/src/mc6847/font-6847t1.c
        ${PICO_COCO_XROAR_DIR}/src/mc6883.c
        ${PICO_COCO_XROAR_DIR}/src/events.c
        ${PICO_COCO_XROAR_DIR}/portalib/delegate.c
        xroar_vdg.c
    )
    target_include_directories(xroar_vdg PRIVATE
        ${PICO_COCO_XROAR_DIR}
        ${PICO_COCO_XROAR_DIR}/src
        ${PICO_COCO_XROAR_DIR}/portalib
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(xroar_vdg PROPERTIES C_EXTENSIONS ON)
    # XRoar's names that are also this project's, renamed in its files.
    target_compile_definitions(xroar_vdg PRIVATE
        mc6847_reset=xr_mc6847_reset
        mc6847_set_mode=xr_mc6847_set_mode
        mc6847_update=xr_mc6847_update
        mc6847_set_inverted_text=xr_mc6847_set_inverted_text
        mc6847_pause=xr_mc6847_pause
        mc6847_unpause=xr_mc6847_unpause
        font_6847=xr_font_6847)

    add_executable(test_vdg_oracle test_vdg_oracle.c)
    target_link_libraries(test_vdg_oracle PRIVATE pico_coco_core xroar_vdg)
    target_include_directories(test_vdg_oracle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_vdg_oracle PRIVATE ${PICO_COCO_WARNINGS})
else()
    add_executable(test_vdg_oracle test_skip.c)
    target_include_directories(test_vdg_oracle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_vdg_oracle PRIVATE
        SKIP_REASON="no XRoar checkout at ${PICO_COCO_XROAR_DIR}: run tools/trace/build-xroar.sh --checkout-only")
endif()
add_test(NAME test_vdg_oracle COMMAND test_vdg_oracle)
set_tests_properties(test_vdg_oracle PROPERTIES SKIP_RETURN_CODE 77 LABELS oracle)

# The golden images (§9.6, EL §5.7). The scenes are shared with vdg-ppm,
# which writes test/golden/, so the writer and the checker cannot drift.
add_library(vdg_scenes STATIC vdg_scenes.c frame_ppm.c)
target_link_libraries(vdg_scenes PUBLIC pico_coco_core)
target_include_directories(vdg_scenes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(vdg_scenes PRIVATE ${PICO_COCO_WARNINGS})

add_executable(test_mc6847_golden test_mc6847_golden.c)
target_link_libraries(test_mc6847_golden PRIVATE vdg_scenes)
target_compile_options(test_mc6847_golden PRIVATE ${PICO_COCO_WARNINGS})
target_compile_definitions(test_mc6847_golden PRIVATE
    PICO_COCO_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/test/golden")
add_test(NAME test_mc6847_golden COMMAND test_mc6847_golden)

add_executable(vdg-ppm ${PROJECT_SOURCE_DIR}/tools/vdg-ppm.c)
target_link_libraries(vdg-ppm PRIVATE vdg_scenes)
target_compile_options(vdg-ppm PRIVATE ${PICO_COCO_WARNINGS})

# Which BASIC each size runs (roms.h): roms.c has no SDK header.
add_executable(test_roms test_roms.c ${PROJECT_SOURCE_DIR}/src/port/roms.c)
target_link_libraries(test_roms PRIVATE pico_coco_core)
target_include_directories(test_roms PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/port)
target_compile_options(test_roms PRIVATE ${PICO_COCO_WARNINGS})
add_test(NAME test_roms COMMAND test_roms)

# The missing-ROM pages, rendered to be looked at (design.md §12.1). roms.c
# is the port's, and includes no SDK header, so the host builds it here.
add_executable(page-ppm ${PROJECT_SOURCE_DIR}/tools/page-ppm.c
               ${PROJECT_SOURCE_DIR}/src/port/roms.c frame_ppm.c)
target_link_libraries(page-ppm PRIVATE pico_coco_core)
target_include_directories(page-ppm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/port)
target_compile_options(page-ppm PRIVATE ${PICO_COCO_WARNINGS})

# The port's audio path, heard (§10): src/port/audio.c itself, built
# against sdk_sim/'s stand-ins for the SDK, whose DMA reads the ring as
# the hardware does. From pico-oric.
add_executable(test_audio_port test_audio_port.c ${PROJECT_SOURCE_DIR}/src/port/audio.c
    sdk_sim/sdk_sim.c)
target_link_libraries(test_audio_port PRIVATE pico_coco_core m)
target_include_directories(test_audio_port BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk_sim
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/port)
target_compile_options(test_audio_port PRIVATE ${PICO_COCO_WARNINGS})
add_test(NAME test_audio_port COMMAND test_audio_port)
