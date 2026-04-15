#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "lzma/api/lzma.h"
#include "xz_writer.h"

#define XZ_OUTBUF_SIZE (64 * 1024)

struct xz_writer {
	FILE *out_fp;
	lzma_stream strm;
	unsigned char outbuf[XZ_OUTBUF_SIZE];
	unsigned long long bytes_out;
	int finished;
};

static int flush_out(xz_writer_t *w)
{
	size_t have = sizeof(w->outbuf) - w->strm.avail_out;
	if (have == 0)
		return 0;
	if (fwrite(w->outbuf, 1, have, w->out_fp) != have) {
		fprintf(stderr, "xz: output write failed: %s\n", strerror(errno));
		return -1;
	}
	w->bytes_out += (unsigned long long)have;
	w->strm.next_out = w->outbuf;
	w->strm.avail_out = sizeof(w->outbuf);
	return 0;
}

xz_writer_t *xz_writer_open(FILE *out_fp, int preset)
{
	xz_writer_t *w = calloc(1, sizeof(*w));
	if (!w)
		return NULL;

	w->out_fp = out_fp;
	/* Cap preset to valid range; allow LZMA_PRESET_EXTREME via high bits if
	 * caller sets them, otherwise clamp. */
	uint32_t plevel;
	if (preset < 0)
		plevel = 6;
	else if ((preset & ~LZMA_PRESET_EXTREME) > 9)
		plevel = 6 | (preset & LZMA_PRESET_EXTREME);
	else
		plevel = (uint32_t)preset;

	lzma_ret rv = lzma_easy_encoder(&w->strm, plevel, LZMA_CHECK_CRC64);
	if (rv != LZMA_OK) {
		fprintf(stderr, "xz: lzma_easy_encoder failed (%d)\n", (int)rv);
		free(w);
		return NULL;
	}

	w->strm.next_out = w->outbuf;
	w->strm.avail_out = sizeof(w->outbuf);
	return w;
}

int xz_writer_write(xz_writer_t *w, const void *buf, size_t len)
{
	if (!w || w->finished)
		return -1;

	w->strm.next_in = (const uint8_t *)buf;
	w->strm.avail_in = len;

	while (w->strm.avail_in > 0) {
		lzma_ret rv = lzma_code(&w->strm, LZMA_RUN);
		if (rv != LZMA_OK) {
			fprintf(stderr, "xz: lzma_code(RUN) failed (%d)\n", (int)rv);
			return -1;
		}
		if (w->strm.avail_out == 0) {
			if (flush_out(w) != 0)
				return -1;
		}
	}
	return 0;
}

int xz_writer_close(xz_writer_t *w)
{
	int rv = 0;
	if (!w)
		return 0;

	if (!w->finished) {
		for (;;) {
			lzma_ret r = lzma_code(&w->strm, LZMA_FINISH);
			if (w->strm.avail_out == 0 || r == LZMA_STREAM_END) {
				if (flush_out(w) != 0) {
					rv = -1;
					break;
				}
			}
			if (r == LZMA_STREAM_END)
				break;
			if (r != LZMA_OK) {
				fprintf(stderr, "xz: lzma_code(FINISH) failed (%d)\n", (int)r);
				rv = -1;
				break;
			}
		}
		w->finished = 1;
	}

	lzma_end(&w->strm);

	if (w->out_fp) {
		if (fclose(w->out_fp) != 0) {
			fprintf(stderr, "xz: fclose failed: %s\n", strerror(errno));
			rv = -1;
		}
		w->out_fp = NULL;
	}
	free(w);
	return rv;
}

unsigned long long xz_writer_bytes_out(xz_writer_t *w)
{
	return w ? w->bytes_out : 0;
}
