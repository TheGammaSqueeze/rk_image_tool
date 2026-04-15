cmake_minimum_required(VERSION 3.16)
project(rk_image_tool
    VERSION 0.1.0
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
    src/common/mbr.c
    src/common/gpt.c
    src/common/misc_cmd.c
    src/common/fat32.c
    src/common/parameter.c
    src/common/sdcard.c
    src/common/util.c
    src/common/disk.c)

add_executable(rk_image_tool
    ${COMMON_SRC}
    ${PLATFORM_SRC}
    src/cli/main.c
    src/cli/cmd_info.c
    src/cli/cmd_unpack.c
    src/cli/cmd_pack.c
    src/cli/cmd_sdboot.c
    src/cli/cmd_upgrade.c
    src/cli/cmd_restore.c
    src/cli/cmd_listdisks.c)

target_include_directories(rk_image_tool PRIVATE src)

if(WIN32)
    target_link_libraries(rk_image_tool PRIVATE setupapi)
endif()

install(TARGETS rk_image_tool RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(tests)
