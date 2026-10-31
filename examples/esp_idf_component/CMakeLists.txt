cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ELF_OVERLAY_DIR)
    get_filename_component(ELF_OVERLAY_DIR "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
endif()

set(EXTRA_COMPONENT_DIRS "${ELF_OVERLAY_DIR}")
if(DEFINED ELF_LOADER_DIR)
    list(APPEND EXTRA_COMPONENT_DIRS "${ELF_LOADER_DIR}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(elf_overlay_device_build)
