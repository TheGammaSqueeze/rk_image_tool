/* Portable config.h for liblzma encoder-only static build.
 * Derived from xz-5.4.7 ./configure output, trimmed to portable, CPU-neutral
 * options so the tree cross-compiles cleanly without autotools.
 */

#ifndef _RK_LIBLZMA_CONFIG_H
#define _RK_LIBLZMA_CONFIG_H

#define ASSUME_RAM 128

#define HAVE_CHECK_CRC32 1
#define HAVE_CHECK_CRC64 1
#define HAVE_CHECK_SHA256 1

#define HAVE_ENCODERS 1
#define HAVE_ENCODER_LZMA1 1
#define HAVE_ENCODER_LZMA2 1

#define HAVE_MF_BT2 1
#define HAVE_MF_BT3 1
#define HAVE_MF_BT4 1
#define HAVE_MF_HC3 1
#define HAVE_MF_HC4 1

#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UINTPTR_T 1

#define HAVE_MBRTOWC 1
#define HAVE_POSIX_FADVISE 1

#define HAVE_VISIBILITY 1
#define HAVE_FUNC_ATTRIBUTE_CONSTRUCTOR 1

#define PACKAGE "xz"
#define PACKAGE_NAME "XZ Utils"
#define PACKAGE_STRING "XZ Utils 5.4.7"
#define PACKAGE_TARNAME "xz"
#define PACKAGE_URL "https://tukaani.org/xz/"
#define PACKAGE_VERSION "5.4.7"
#define PACKAGE_BUGREPORT "xz@tukaani.org"
#define VERSION "5.4.7"

#define STDC_HEADERS 1

#define TUKLIB_PHYSMEM_SYSCONF 1
#define TUKLIB_CPUCORES_SYSCONF 1

#endif /* _RK_LIBLZMA_CONFIG_H */
