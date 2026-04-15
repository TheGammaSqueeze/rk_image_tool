cmake_minimum_required(VERSION 3.16)
project(rk_image_tool
    VERSION 0.2.0
    DESCRIPTION "Cross-platform Rockchip SD Firmware / Upgrade Disk tool"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-function)
endif()

option(RK_STATIC "Produce a fully static binary" OFF)
if(RK_STATIC AND NOT MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

option(RK_TUI "Build curses-based TUI front-end" ON)

add_subdirectory(third_party)

if(WIN32)
    set(PLATFORM_SRC src/platform/disk_windows.c)
elseif(APPLE)
    set(PLATFORM_SRC src/platform/disk_macos.c)
else()
    set(PLATFORM_SRC src/platform/disk_linux.c)
endif()

set(COMMON_SRC
    src/common/md5.c
    src/common/rc4.c
    src/common/crc.c
    src/common/idblock.c
    src/common/rkimg.c
    src/common/rkpack.c
    src/common/mbr.c
    src/common/gpt.c
    src/common/misc_cmd.c
    src/common/fat32.c
    src/common/parameter.c
    src/common/sdcard.c
    src/common/util.c
    src/common/disk.c
    src/common/sink.c
    src/common/xz_writer.c
    src/common/progress.c)

set(CLI_SRC
    src/cli/main.c
    src/cli/cmd_info.c
    src/cli/cmd_unpack.c
    src/cli/cmd_pack.c
    src/cli/cmd_sdboot.c
    src/cli/cmd_upgrade.c
    src/cli/cmd_restore.c
    src/cli/cmd_listdisks.c
    src/cli/cmd_verify.c)

if(RK_TUI)
    list(APPEND CLI_SRC src/cli/cmd_tui.c)
endif()

add_executable(rk_image_tool
    ${COMMON_SRC}
    ${PLATFORM_SRC}
    ${CLI_SRC})

target_include_directories(rk_image_tool PRIVATE src third_party)
target_link_libraries(rk_image_tool PRIVATE rk_lzma)

if(RK_TUI)
    target_compile_definitions(rk_image_tool PRIVATE RK_HAVE_TUI=1)
endif()

if(WIN32)
    target_link_libraries(rk_image_tool PRIVATE setupapi)
    target_compile_definitions(rk_image_tool PRIVATE _WIN32_WINNT=0x0601)
endif()

install(TARGETS rk_image_tool RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(tests)
