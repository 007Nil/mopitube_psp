cmake_minimum_required(VERSION 3.20)

# Point at the pspdev toolchain before project()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{PSPDEV})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/cmake/psp-toolchain.cmake"
        CACHE STRING "pspdev toolchain")
  else()
    message(FATAL_ERROR "Set the PSPDEV environment variable or pass -DCMAKE_TOOLCHAIN_FILE=...")
  endif()
endif()

project(MopiTube C)

add_executable(MopiTube
    src/main.c
    src/config.c
    src/net.c
    src/mpd.c
    src/input.c
    src/ui.c
)

target_compile_options(MopiTube PRIVATE -O2 -G0 -Wall -Wextra -std=c99)

target_link_libraries(MopiTube
    pspdebug pspdisplay pspge pspgu
    pspctrl
    pspnet pspnet_inet pspnet_apctl pspnet_resolver psputility
    psprtc
    m
)

create_pbp_file(
    TARGET      MopiTube
    TITLE       "MopiTube"
    ICON_PATH   NULL
    PREVIEW_PATH NULL
    PRINT_GPROF_ENABLED FALSE
)
