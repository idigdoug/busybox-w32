/* vi: set sw=4 ts=4: */
#ifndef WIN32_STRCONV_H
#define WIN32_STRCONV_H

#include <stdbool.h>

/*
 * String conversion between multibyte (char) and wide character (wchar_t)
 * strings, using a configurable code page.
 *
 * Conversion functions take a caller-provided stack buffer and its size.
 * If the result fits, the stack buffer is used; otherwise a heap buffer
 * is allocated via xmalloc.  The result struct indicates whether the
 * returned string needs to be freed.
 */

/*
 * Result returned from bb_to_wcs, bb_to_wcs_n. Free with wcs_free.
 */
typedef struct {
	wchar_t *str;
	bool need_to_free;
} wcs_result;

/*
 * Result returned from bb_to_mbs, bb_to_mbs_n. Free with mbs_free.
 */
typedef struct {
	char *str;
	bool need_to_free;
} mbs_result;

/*
 * Changes the Windows code page used by bb_to_wcs and bb_to_mbs.
 * Default is CP_UTF8.
 */
void bb_set_codepage(UINT cp);

/*
 * Retrieves the Windows code page used by bb_to_wcs and bb_to_mbs.
 * Default is CP_UTF8.
 */
UINT bb_get_codepage(void);

/*
 * Convert a NUL-terminated multibyte string to wide characters.
 * buf/buf_bytes: caller-provided buffer and its size in bytes.
 * If buf_bytes is 0, a heap buffer is always allocated.
 */
wcs_result bb_to_wcs(const char *s, wchar_t *buf, int buf_bytes);

/*
 * Convert a counted (not necessarily NUL-terminated) multibyte string
 * to wide characters.  The result is always NUL-terminated.
 * slen is the number of bytes in s to convert.
 */
wcs_result bb_to_wcs_n(const char *s, int slen, wchar_t *buf, int buf_bytes);

/*
 * Convert a NUL-terminated wide string to multibyte characters.
 * buf/buf_bytes: caller-provided buffer and its size in bytes.
 * If buf_bytes is 0, a heap buffer is always allocated.
 */
mbs_result bb_to_mbs(const wchar_t *ws, char *buf, int buf_bytes);

/*
 * Convert a counted (not necessarily NUL-terminated) wide string
 * to multibyte characters.  The result is always NUL-terminated.
 * wlen is the number of wchar_t elements in ws to convert.
 * buf_bytes is the size of buf in bytes.
 */
mbs_result bb_to_mbs_n(const wchar_t *ws, int wlen, char *buf, int buf_bytes);

/*
 *  Frees the buffer returned by bb_to_wcs or bb_to_wcs_n as necessary.
 */
void wcs_free(wcs_result *r);

/*
 *  Frees the buffer returned by bb_to_mbs or bb_to_mbs_n as necessary.
 */
void mbs_free(mbs_result *r);

#endif /* WIN32_STRCONV_H */
