# pico-coco — one source tree, two targets (design.md §6):
#   - the UF2 for the PicoCalc (default)
#   - a host test build that compiles src/core/ with the system compiler
#     and no Pico SDK, behind -DPICO_COCO_HOST=ON, run under CTest.
#
# The core building clean under -Wall -Wextra -Werror on both toolchains
# is the mechanism that keeps SDK dependencies from leaking downwards
# (EL §2.1).

cmake_minimum_required(VERSION 3.18)

option(PICO_COCO_HOST "Build the portable core and its host tests, with no Pico SDK" OFF)

# Every fixed capacity lives in src/core/config.h; nothing here duplicates one.
set(PICO_COCO_CORE_SOURCES
    src/core/mc6809.c
)

set(PICO_COCO_WARNINGS -Wall -Wextra -Werror)

# The indexed decoder called rather than inlined into each opcode. On since
# M2's gate: 34 KiB of interpreter in SRAM at tier 2 against 88, within
# design.md §16's budget, for 6-8 % more host cycles an instruction; and
# 63 % of core 0 on the mix at tier 0 against 101 % (design.md §7.5).
option(PICO_COCO_IDX_OUTLINE "The CPU's indexed decoder out of line" ON)
if (PICO_COCO_IDX_OUTLINE)
    set(PICO_COCO_IDX_OUTLINE_ON 1)
else()
    set(PICO_COCO_IDX_OUTLINE_ON 0)
endif()

# The core as a library both targets link. Headers only while it has no
# sources; a STATIC library, built with our warnings, once it has.
function(pico_coco_core_library)
    if (PICO_COCO_CORE_SOURCES)
        add_library(pico_coco_core STATIC ${PICO_COCO_CORE_SOURCES})
        target_include_directories(pico_coco_core PUBLIC src/core)
        target_compile_options(pico_coco_core PRIVATE ${PICO_COCO_WARNINGS})
        target_compile_definitions(pico_coco_core PRIVATE
            PICO_COCO_IDX_OUTLINE=${PICO_COCO_IDX_OUTLINE_ON})
    else()
        add_library(pico_coco_core INTERFACE)
        target_include_directories(pico_coco_core INTERFACE src/core)
    endif()
endfunction()

# M2's workloads, shared by the host test and the bench firmware
# (design.md §7.5). Portable like the core, and the slow path's owner.
function(pico_coco_bench_library)
    add_library(pico_coco_bench STATIC src/bench/bench.c)
    target_include_directories(pico_coco_bench PUBLIC src/bench)
    target_link_libraries(pico_coco_bench PUBLIC pico_coco_core)
    target_compile_options(pico_coco_bench PRIVATE ${PICO_COCO_WARNINGS})
endfunction()

if (PICO_COCO_HOST)
    project(pico_coco_host C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    pico_coco_core_library()
    pico_coco_bench_library()

    enable_testing()
    add_subdirectory(test/host)
else()
    # One pico2 image for every RP2350 board (design.md §4, D3).
    if (NOT DEFINED PICO_BOARD)
        set(PICO_BOARD pico2)
    endif()
    if (NOT DEFINED PICO_PLATFORM AND PICO_BOARD STREQUAL "pico2")
        set(PICO_PLATFORM rp2350)
    endif()

    include(cmake/pico_sdk_import.cmake)

    project(pico_coco C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    pico_sdk_init()

    pico_coco_core_library()
    pico_coco_bench_library()

    # The firmware's version, git describe --always --dirty, for the
    # banner and the About page (design.md §14). A target that always
    # runs, so it is taken at build time; the header only changes when
    # the text does.
    set(PICO_COCO_GENERATED ${CMAKE_BINARY_DIR}/generated)
    add_custom_target(pico_coco_version ALL
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                -DOUT=${PICO_COCO_GENERATED}/pico_coco_version.h
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake
        BYPRODUCTS ${PICO_COCO_GENERATED}/pico_coco_version.h
        COMMENT "pico-coco: version from git describe"
        VERBATIM)

    # PICO_COCO_UART=OFF is the build that ships: nothing on UART1, so GP4
    # and GP5 are free. ON, the default, is the development build, which
    # uart-log.sh and flash.sh's checks need (design.md D7,
    # hardware-notes.md §2.7).
    option(PICO_COCO_UART "Log over the debug UART (development)" ON)
    if (PICO_COCO_UART)
        set(PICO_COCO_UART_ON 1)
    else()
        set(PICO_COCO_UART_ON 0)
    endif()

    # Which debug UART: 1 is UART1 on GP4/GP5, D7's default; 0 is UART0 on
    # GP2/GP3 through UART_AUX, for a board with a DS3231 on i2c0 at
    # GP4/GP5 (hardware-notes.md §1.1).
    set(PICO_COCO_UART_PORT 1 CACHE STRING "Debug UART: 1 = GP4/GP5, 0 = GP2/GP3")
    if (PICO_COCO_UART_PORT STREQUAL "1")
        set(PICO_COCO_UART_TX 4)
        set(PICO_COCO_UART_RX 5)
    elseif (PICO_COCO_UART_PORT STREQUAL "0")
        set(PICO_COCO_UART_TX 2)
        set(PICO_COCO_UART_RX 3)
    else()
        message(FATAL_ERROR "PICO_COCO_UART_PORT is 0 or 1, not ${PICO_COCO_UART_PORT}")
    endif()

    # How much of the core's hot path runs from SRAM (src/core/hot.h). 2
    # since M2's gate: the mix took 101 % of core 0 at tier 0 and 13 % at
    # tier 2 (design.md §7.5, docs/milestones.md); M11 measures it again
    # with core 1 presenting. A build directory configured before keeps
    # its cached value until it is passed once (hardware-notes.md §9.8).
    set(PICO_COCO_RAM_TIER 2 CACHE STRING "Hot-code SRAM tier, 0-2 (src/core/hot.h)")
    if (PICO_COCO_CORE_SOURCES)
        # The core library needs it too: that is where the marked functions are.
        target_compile_definitions(pico_coco_core PRIVATE PICO_COCO_RAM_TIER=${PICO_COCO_RAM_TIER})
    endif()

    get_filename_component(PICO_COCO_TOOLCHAIN_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(PICO_COCO_SIZE arm-none-eabi-size HINTS ${PICO_COCO_TOOLCHAIN_BIN} REQUIRED)

    # What every image shares: the port's include path, the version, the
    # UART, the tier, the clocks, and its size printed on every build.
    function(pico_coco_firmware target)
        target_include_directories(${target} PRIVATE src/port ${PICO_COCO_GENERATED})
        target_compile_options(${target} PRIVATE ${PICO_COCO_WARNINGS})
        add_dependencies(${target} pico_coco_version)

        # clk_peri does not follow clk_sys by default; the SPI baud and the
        # audio carrier are re-applied in code as well (hardware-notes.md §3).
        target_compile_definitions(${target} PRIVATE
            PICO_COCO_UART=${PICO_COCO_UART_ON}
            PICO_COCO_RAM_TIER=${PICO_COCO_RAM_TIER}
            PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK=1
            # 115200 8-N-1 on the pins chosen above (hardware-notes.md §1.1,
            # §2.7); the SDK picks UART_AUX for GP2/GP3 itself.
            PICO_DEFAULT_UART=${PICO_COCO_UART_PORT}
            PICO_DEFAULT_UART_TX_PIN=${PICO_COCO_UART_TX}
            PICO_DEFAULT_UART_RX_PIN=${PICO_COCO_UART_RX}
        )

        target_link_libraries(${target}
            pico_coco_core
            pico_stdlib
            pico_status_led
            pico_unique_id
            hardware_adc
            hardware_clocks
            hardware_vreg
        )

        pico_enable_stdio_uart(${target} ${PICO_COCO_UART_ON})
        pico_enable_stdio_usb(${target} 0)

        pico_add_extra_outputs(${target})

        # The image's size on every build (design.md §6, EL §2.1).
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${PICO_COCO_SIZE} $<TARGET_FILE:${target}>
            VERBATIM)
    endfunction()

    # The emulator. M0: the banner and a heartbeat (design.md §17).
    add_executable(pico-coco
        src/port/board.c
        src/port/main.c
    )
    pico_coco_firmware(pico-coco)

    # M2's bench: the 6809 alone, timed on the basic and mix workloads
    # (design.md §7.5, §17). One core, no LCD, keyboard or audio.
    add_executable(pico-coco-bench
        src/port/board.c
        src/port/bench_main.c
    )
    pico_coco_firmware(pico-coco-bench)
    target_link_libraries(pico-coco-bench pico_coco_bench)
    target_compile_definitions(pico-coco-bench PRIVATE
        PICO_COCO_IDX_OUTLINE=${PICO_COCO_IDX_OUTLINE_ON})
endif()
