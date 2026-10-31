cmake_minimum_required(VERSION 3.16)
project(elf_overlay VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(ELF_OVERLAY_BUILD_TESTS "Build tests" ON)
option(ELF_OVERLAY_BUILD_TOOLS "Build host tools" ON)
option(ELF_OVERLAY_ESP_IDF "Build ESP-IDF port and esp_elf_loader integration" OFF)
option(ELF_OVERLAY_ESP_ELF_INCLUDE "Path to esp_elf.h parent directory" "")
option(ELF_OVERLAY_WERROR "Treat warnings as errors (CI)" OFF)
option(ELF_OVERLAY_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(MSVC)
    set(ELF_OVERLAY_WARN_FLAGS /W4)
    if(ELF_OVERLAY_WERROR)
        list(APPEND ELF_OVERLAY_WARN_FLAGS /WX)
    endif()
else()
    set(ELF_OVERLAY_WARN_FLAGS -Wall -Wextra -Wpedantic -Wshadow
                               -Wpointer-arith -Wcast-qual -Wstrict-prototypes)
    if(ELF_OVERLAY_WERROR)
        list(APPEND ELF_OVERLAY_WARN_FLAGS -Werror)
    endif()
endif()

# Sanitizers for host builds. Applied as compile and link flags so the runtime
# shims are linked in. Not compatible with the ESP-IDF target.
if(ELF_OVERLAY_ASAN)
    if(ELF_OVERLAY_ESP_IDF)
        message(FATAL_ERROR "ELF_OVERLAY_ASAN is host-only; disable ELF_OVERLAY_ESP_IDF")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

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
target_compile_options(elf_overlay PRIVATE ${ELF_OVERLAY_WARN_FLAGS})

function(elf_overlay_add_exe name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} elf_overlay)
    target_compile_options(${name} PRIVATE ${ELF_OVERLAY_WARN_FLAGS})
endfunction()

# Tests
if(ELF_OVERLAY_BUILD_TESTS)
    enable_testing()
    elf_overlay_add_exe(test_isa tests/unit/test_isa.c)
    add_test(NAME isa_test COMMAND test_isa)
    elf_overlay_add_exe(test_blob tests/unit/test_blob.c)
    add_test(NAME blob_test COMMAND test_blob)
    elf_overlay_add_exe(test_l32r tests/unit/test_l32r.c)
    add_test(NAME l32r_test COMMAND test_l32r)
    elf_overlay_add_exe(test_load tests/host/test_load.c)
    add_test(NAME host_load_test COMMAND test_load ${CMAKE_SOURCE_DIR}/tests/integration/fixtures/minimal.elfo)
    elf_overlay_add_exe(test_fixture tests/host/test_fixture.c)
    add_test(NAME host_fixture_test COMMAND test_fixture ${CMAKE_SOURCE_DIR}/tests/integration/c_fixtures/fixture.elfo)
    elf_overlay_add_exe(test_redirect tests/unit/test_redirect.c)
    add_test(NAME redirect_test COMMAND test_redirect)
    elf_overlay_add_exe(test_hostref tests/unit/test_hostref.c)
    add_test(NAME hostref_test COMMAND test_hostref)
endif()

# Tools
if(ELF_OVERLAY_BUILD_TOOLS)
    elf_overlay_add_exe(elf_overlay_relink tools/relink/main.c
        tools/common/elf_read.c tools/common/reloc.c)
    elf_overlay_add_exe(elf_overlay_emit tools/emit/main.c)
endif()
