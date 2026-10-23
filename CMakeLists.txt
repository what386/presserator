cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "/opt/esp-idf")
endif()

if(NOT EXISTS "$ENV{IDF_PATH}/tools/cmake/project.cmake")
    message(FATAL_ERROR
        "ESP-IDF was not found at $ENV{IDF_PATH}. Install it there or set IDF_PATH in .envrc, then run 'direnv allow'."
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(presserator)
