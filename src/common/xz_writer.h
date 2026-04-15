#ifndef _RK_XZ_WRITER_H
#define _RK_XZ_WRITER_H

#include <stdio.h>
#include <stddef.h>

typedef struct xz_writer xz_writer_t;

/* Open an xz-compressed writer backed by an output FILE*. Takes ownership of
 * out_fp in the sense that xz_writer_close() flushes and closes it.
 * preset: 0-9 (6 is xz's default). */
xz_writer_t *xz_writer_open(FILE *out_fp, int preset);

/* Write raw uncompressed bytes. Returns 0 on success. */
int xz_writer_write(xz_writer_t *w, const void *buf, size_t len);

/* Finish the xz stream and free resources. Returns 0 on success. Closes the
 * underlying FILE*. */
int xz_writer_close(xz_writer_t *w);

/* Returns the total compressed bytes written so far. */
unsigned long long xz_writer_bytes_out(xz_writer_t *w);

#endif /* _RK_XZ_WRITER_H */
