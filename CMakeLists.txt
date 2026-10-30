cmake_minimum_required(VERSION 3.16)
project(elf_overlay VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(ELF_OVERLAY_BUILD_TESTS "Build tests" ON)
option(ELF_OVERLAY_BUILD_TOOLS "Build host tools" ON)
option(ELF_OVERLAY_ESP_IDF "Build ESP-IDF port and esp_elf_loader integration" OFF)
option(ELF_OVERLAY_ESP_ELF_INCLUDE "Path to esp_elf.h parent directory" "")

include_directories(include tools/common)

# Core library
add_library(elf_overlay
    src/isa/isa.c
    src/isa/xtensa/patch.c
    src/isa/riscv/patch.c
    src/core/blob.c
    src/core/loader.c
    src/port/port.c
    tools/common/blob_pack.c
)

# esp_elf_loader integration (device builds only)
if(ELF_OVERLAY_ESP_IDF)
    target_sources(elf_overlay PRIVATE
        src/core/esp_elf_integration.c
    )
    if(ELF_OVERLAY_ESP_ELF_INCLUDE)
        target_include_directories(elf_overlay PRIVATE ${ELF_OVERLAY_ESP_ELF_INCLUDE})
    endif()
endif()

target_include_directories(elf_overlay PUBLIC include)

# Tests
if(ELF_OVERLAY_BUILD_TESTS)
    enable_testing()
    add_executable(test_isa tests/unit/test_isa.c)
    target_link_libraries(test_isa elf_overlay)
    add_test(NAME isa_test COMMAND test_isa)
    add_executable(test_blob tests/unit/test_blob.c)
    target_link_libraries(test_blob elf_overlay)
    add_test(NAME blob_test COMMAND test_blob)
    add_executable(test_l32r tests/unit/test_l32r.c)
    target_link_libraries(test_l32r elf_overlay)
    add_test(NAME l32r_test COMMAND test_l32r)
    add_executable(test_load tests/host/test_load.c)
    target_link_libraries(test_load elf_overlay)
    add_test(NAME host_load_test COMMAND test_load ${CMAKE_SOURCE_DIR}/tests/integration/fixtures/minimal.elfo)
    add_executable(test_fixture tests/host/test_fixture.c)
    target_link_libraries(test_fixture elf_overlay)
    add_test(NAME host_fixture_test COMMAND test_fixture ${CMAKE_SOURCE_DIR}/tests/integration/c_fixtures/fixture.elfo)
    add_executable(test_redirect tests/unit/test_redirect.c)
    target_link_libraries(test_redirect elf_overlay)
    add_test(NAME redirect_test COMMAND test_redirect)
endif()

# Tools
if(ELF_OVERLAY_BUILD_TOOLS)
    add_executable(elf_overlay_relink tools/relink/main.c tools/common/elf_read.c tools/common/reloc.c)
    target_link_libraries(elf_overlay_relink elf_overlay)
    add_executable(elf_overlay_emit tools/emit/main.c)
    target_link_libraries(elf_overlay_emit elf_overlay)
endif()
