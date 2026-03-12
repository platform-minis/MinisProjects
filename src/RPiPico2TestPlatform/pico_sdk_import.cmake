# pico_sdk_import.cmake
# Standard Pico SDK import script.
# Source: https://github.com/raspberrypi/pico-sdk/blob/master/external/pico_sdk_import.cmake
#
# This is a copy of the file from pico-sdk/external/.
# Alternatively set PICO_SDK_PATH and include this file in your project.

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    message(STATUS "Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
endif ()

if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT} AND (NOT PICO_SDK_FETCH_FROM_GIT))
    set(PICO_SDK_FETCH_FROM_GIT $ENV{PICO_SDK_FETCH_FROM_GIT})
endif ()

if (NOT PICO_SDK_PATH)
    if (PICO_SDK_FETCH_FROM_GIT)
        include(FetchContent)
        set(FETCHCONTENT_BASE_DIR_SAVE ${FETCHCONTENT_BASE_DIR})
        set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)
        FetchContent_Declare(
            pico_sdk
            GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk
            GIT_TAG 2.1.0
        )
        FetchContent_GetProperties(pico_sdk)
        if (NOT pico_sdk_POPULATED)
            FetchContent_Populate(pico_sdk)
            set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
        endif()
        set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
    else ()
        message(FATAL_ERROR
            "pico-sdk location not specified. "
            "Set PICO_SDK_PATH or PICO_SDK_FETCH_FROM_GIT=1."
        )
    endif ()
endif ()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Pico SDK")
set(PICO_SDK_INIT_CMAKE_FILE ${PICO_SDK_PATH}/pico_sdk_init.cmake)

if (NOT EXISTS ${PICO_SDK_INIT_CMAKE_FILE})
    message(FATAL_ERROR
        "Directory '${PICO_SDK_PATH}' does not appear to contain the Pico SDK. "
        "Set PICO_SDK_PATH to the root directory of the pico-sdk repository."
    )
endif ()

include(${PICO_SDK_INIT_CMAKE_FILE})
