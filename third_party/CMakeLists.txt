file(GLOB_RECURSE LZMA_SOURCES
    lzma/check/check.c
    lzma/check/crc32_fast.c
    lzma/check/crc32_table.c
    lzma/check/crc64_fast.c
    lzma/check/crc64_table.c
    lzma/check/sha256.c
    lzma/common/alone_encoder.c
    lzma/common/block_buffer_encoder.c
    lzma/common/block_encoder.c
    lzma/common/block_header_encoder.c
    lzma/common/block_util.c
    lzma/common/common.c
    lzma/common/easy_buffer_encoder.c
    lzma/common/easy_encoder.c
    lzma/common/easy_encoder_memusage.c
    lzma/common/easy_preset.c
    lzma/common/filter_buffer_encoder.c
    lzma/common/filter_common.c
    lzma/common/filter_encoder.c
    lzma/common/filter_flags_encoder.c
    lzma/common/hardware_physmem.c
    lzma/common/index.c
    lzma/common/index_encoder.c
    lzma/common/stream_buffer_encoder.c
    lzma/common/stream_encoder.c
    lzma/common/stream_flags_common.c
    lzma/common/stream_flags_encoder.c
    lzma/common/string_conversion.c
    lzma/common/tuklib_physmem.c
    lzma/common/vli_encoder.c
    lzma/common/vli_size.c
    lzma/lz/lz_encoder.c
    lzma/lz/lz_encoder_mf.c
    lzma/lzma/fastpos_table.c
    lzma/lzma/lzma2_encoder.c
    lzma/lzma/lzma_encoder.c
    lzma/lzma/lzma_encoder_optimum_fast.c
    lzma/lzma/lzma_encoder_optimum_normal.c
    lzma/lzma/lzma_encoder_presets.c
    lzma/rangecoder/price_table.c)

add_library(rk_lzma STATIC ${LZMA_SOURCES})
target_include_directories(rk_lzma PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/api
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/common
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/check
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/lz
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/lzma
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/rangecoder
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/delta
    ${CMAKE_CURRENT_SOURCE_DIR}/lzma/simple)
target_compile_definitions(rk_lzma PRIVATE
    HAVE_CONFIG_H
    TUKLIB_SYMBOL_PREFIX=lzma_)

if(NOT MSVC)
    target_compile_options(rk_lzma PRIVATE -w)
endif()

if(WIN32)
    target_compile_definitions(rk_lzma PRIVATE _WIN32_WINNT=0x0601)
endif()
