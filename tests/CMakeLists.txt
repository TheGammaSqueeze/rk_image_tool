add_executable(test_basic
    test_basic.c
    ../src/common/md5.c
    ../src/common/rc4.c
    ../src/common/crc.c
    ../src/common/idblock.c
    ../src/common/parameter.c
    ../src/common/mbr.c
    ../src/common/gpt.c
    ../src/common/misc_cmd.c)
target_include_directories(test_basic PRIVATE ../src)
add_test(NAME basic COMMAND test_basic)
