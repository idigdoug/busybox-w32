/* vi: set sw=4 ts=4: */
#ifndef WIN32_MINGW_ENCODING_H
#define WIN32_MINGW_ENCODING_H

#include <stdbool.h>
#include <wchar.h>

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
 * Result returned from mingw_to_wcs, mingw_to_wcs_n. Free with mingw_wcs_free.
 */
typedef struct {
	wchar_t *str;
	bool need_to_free;
} mingw_wcs_result_t;

/*
 * Result returned from mingw_to_mbs, mingw_to_mbs_n. Free with mingw_mbs_free.
 */
typedef struct {
	char *str;
	bool need_to_free;
} mingw_mbs_result_t;

/*
 * Codepage encoding type classification.
 */
typedef enum {
	MINGW_CODEPAGE_UTF8,     /* UTF-8: 1-4 byte sequences with well-known structure */
	MINGW_CODEPAGE_SBCS,     /* Single-byte: every byte is one character */
	MINGW_CODEPAGE_DBCS,     /* Double-byte: lead bytes start 2-byte sequences */
	MINGW_CODEPAGE_OTHER,    /* Other multi-byte (e.g. GB18030): not fully-supported */
} mingw_codepage_category;

/*
 * Initialize the encoding subsystem: parse BB_CODEPAGE from the environment,
 * convert wargv to multibyte argv.  Called once at startup from wmain.
 */
char **mingw_encoding_init(wchar_t **wargv);

/*
 * Retrieves the Windows code page used by mingw_to_wcs and mingw_to_mbs.
 * Default is CP_UTF8.
 */
unsigned mingw_get_codepage(void);

/*
 * Returns the classification of the current codepage.
 */
mingw_codepage_category mingw_get_codepage_category(void);

/*
 * Returns MaxCharSize for the current codepage (cached from GetCPInfo).
 */
unsigned mingw_get_codepage_max_charsize(void);

/*
 * For DBCS codepages, returns TRUE if 'c' is a lead byte.
 * Returns FALSE for non-DBCS codepages or non-lead bytes.
 */
bool mingw_is_lead_byte(unsigned char c);

/*
 * Convert a NUL-terminated multibyte string to wide characters.
 * buf/buf_bytes: caller-provided buffer and its size in bytes.
 * If buf_bytes is 0, a heap buffer is always allocated.
 */
mingw_wcs_result_t mingw_to_wcs(const char *s, wchar_t *buf, int buf_bytes);

/*
 * Convert a counted (not necessarily NUL-terminated) multibyte string
 * to wide characters.  The result is always NUL-terminated.
 * slen is the number of bytes in s to convert.
 */
mingw_wcs_result_t mingw_to_wcs_n(const char *s, int slen, wchar_t *buf, int buf_bytes);

/*
 * Convert a NUL-terminated wide string to multibyte characters.
 * buf/buf_bytes: caller-provided buffer and its size in bytes.
 * If buf_bytes is 0, a heap buffer is always allocated.
 */
mingw_mbs_result_t mingw_to_mbs(const wchar_t *ws, char *buf, int buf_bytes);

/*
 * Convert a counted (not necessarily NUL-terminated) wide string
 * to multibyte characters.  The result is always NUL-terminated.
 * wlen is the number of wchar_t elements in ws to convert.
 * buf_bytes is the size of buf in bytes.
 */
mingw_mbs_result_t mingw_to_mbs_n(const wchar_t *ws, int wlen, char *buf, int buf_bytes);

/*
 *  Frees the buffer returned by mingw_to_wcs or mingw_to_wcs_n as necessary.
 */
void mingw_wcs_free(mingw_wcs_result_t *r);

/*
 *  Frees the buffer returned by mingw_to_mbs or mingw_to_mbs_n as necessary.
 */
void mingw_mbs_free(mingw_mbs_result_t *r);

#endif /* WIN32_MINGW_ENCODING_H */
