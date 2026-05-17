/* vi: set sw=4 ts=4: */
/*
 * String conversion between multibyte (char) and wide character (wchar_t)
 * strings, using a configurable code page.
 */
#include "libbb.h"
#include "strconv.h"
#include <assert.h>

static UINT bb_codepage = CP_UTF8;
static enum bb_codepage_type bb_cp_type = BB_CP_UTF8;
static UINT bb_cp_max_charsize = 4; /* MaxCharSize for current codepage */
/* Lead byte table for DBCS: 256-bit bitmap, indexed by byte value */
static unsigned char bb_lead_byte_map[32];

void bb_set_codepage(UINT cp)
{
	CPINFO info;

	memset(bb_lead_byte_map, 0, sizeof(bb_lead_byte_map));

	if (cp == CP_UTF8) {
		bb_codepage = cp;
		bb_cp_type = BB_CP_UTF8;
		bb_cp_max_charsize = 4;
		return;
	}

	if (!GetCPInfo(cp, &info)) {
		bb_error_msg("codepage %u is not supported, keeping %u",
				cp, bb_codepage);
		return;
	}

	bb_codepage = cp;
	bb_cp_max_charsize = info.MaxCharSize;

	if (info.MaxCharSize == 1) {
		bb_cp_type = BB_CP_SBCS;
	} else if (info.MaxCharSize == 2) {
		bb_cp_type = BB_CP_DBCS;
		/* Build lead byte bitmap from the LeadByte ranges.
		 * LeadByte is an array of pairs [low, high], terminated by [0,0]. */
		for (int i = 0; i < MAX_LEADBYTES && info.LeadByte[i]; i += 2) {
			for (unsigned c = info.LeadByte[i]; c <= info.LeadByte[i+1]; c++)
				bb_lead_byte_map[c >> 3] |= (1 << (c & 7));
		}
	} else {
		bb_cp_type = BB_CP_OTHER;
	}
}

UINT bb_get_codepage(void)
{
	return bb_codepage;
}

enum bb_codepage_type bb_get_codepage_type(void)
{
	return bb_cp_type;
}

UINT bb_get_codepage_max_charsize(void)
{
	return bb_cp_max_charsize;
}

BOOL bb_is_lead_byte(unsigned char c)
{
	if (bb_cp_type != BB_CP_DBCS)
		return FALSE;
	return (bb_lead_byte_map[c >> 3] & (1 << (c & 7))) != 0;
}

/*
 * Convert a NUL-terminated multibyte string to wide characters.
 * Tries the caller-provided buffer first; falls back to heap allocation.
 */
wcs_result bb_to_wcs(const char *s, wchar_t *buf, int buf_bytes)
{
	wcs_result r;
	int buf_wchars = buf_bytes / sizeof(wchar_t);
	int n;

	/* NULL input: return NULL output */
	if (s == NULL) {
		r.str = NULL;
		r.need_to_free = false;
		return r;
	}

	/* Optimistic: try to convert directly into the provided buffer */
	if (buf_wchars > 0) {
		n = MultiByteToWideChar(bb_codepage, 0, s, -1, buf, buf_wchars);
		if (n > 0) {
			r.str = buf;
			r.need_to_free = false;
			return r;
		}
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			errno = err_win_to_posix();
			bb_error_msg_and_die("string conversion failed");
		}
	}

	/* Fallback: query required size, allocate, convert */
	n = MultiByteToWideChar(bb_codepage, 0, s, -1, NULL, 0);
	if (n == 0) {
		errno = err_win_to_posix();
		bb_error_msg_and_die("string conversion failed");
	}
	r.str = xmalloc((size_t)n * sizeof(wchar_t));
	r.need_to_free = true;
	MultiByteToWideChar(bb_codepage, 0, s, -1, r.str, n);
	return r;
}

/*
 * Convert a counted multibyte string (slen bytes, not necessarily
 * NUL-terminated) to a NUL-terminated wide string.
 */
wcs_result bb_to_wcs_n(const char *s, int slen, wchar_t *buf, int buf_bytes)
{
	wcs_result r;
	int buf_wchars = buf_bytes / sizeof(wchar_t);
	int n;

	/* NULL input with zero length: return NULL output */
	if (s == NULL && slen == 0) {
		r.str = NULL;
		r.need_to_free = false;
		return r;
	}

	/* Empty input: return an empty NUL-terminated wide string */
	if (slen == 0) {
        /* Note: Not setting str = L"" because str is mutable. */
		if (buf_wchars > 0) {
			r.str = buf;
			r.need_to_free = false;
		} else {
			r.str = xmalloc(sizeof(wchar_t));
			r.need_to_free = true;
		}
        r.str[0] = L'\0';
		return r;
	}

	/*
	 * Optimistic: try to convert into the provided buffer.
	 * Reserve one wchar for the NUL terminator.
	 */
	if (buf_wchars > 1) {
		n = MultiByteToWideChar(bb_codepage, 0, s, slen, buf, buf_wchars - 1);
		if (n > 0) {
			buf[n] = L'\0';
			r.str = buf;
			r.need_to_free = false;
			return r;
		}
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			errno = err_win_to_posix();
			bb_error_msg_and_die("string conversion failed");
		}
	}

	/* Fallback: query required size, allocate, convert */
	n = MultiByteToWideChar(bb_codepage, 0, s, slen, NULL, 0);
	if (n == 0) {
		errno = err_win_to_posix();
		bb_error_msg_and_die("string conversion failed");
	}
	r.str = xmalloc(((size_t)n + 1) * sizeof(wchar_t));
	r.need_to_free = true;
	MultiByteToWideChar(bb_codepage, 0, s, slen, r.str, n);
	r.str[n] = L'\0';
	return r;
}

/*
 * Convert a NUL-terminated wide string to multibyte characters.
 * Tries the caller-provided buffer first; falls back to heap allocation.
 */
mbs_result bb_to_mbs(const wchar_t *ws, char *buf, int buf_bytes)
{
	mbs_result r;
	int n;

	/* NULL input: return NULL output */
	if (ws == NULL) {
		r.str = NULL;
		r.need_to_free = false;
		return r;
	}

	/* Optimistic: try to convert directly into the provided buffer */
	if (buf_bytes > 0) {
		n = WideCharToMultiByte(bb_codepage, 0, ws, -1, buf, buf_bytes,
				NULL, NULL);
		if (n > 0) {
			r.str = buf;
			r.need_to_free = false;
			return r;
		}
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			errno = err_win_to_posix();
			bb_error_msg_and_die("string conversion failed");
		}
	}

	/* Fallback: query required size, allocate, convert */
	n = WideCharToMultiByte(bb_codepage, 0, ws, -1, NULL, 0, NULL, NULL);
	if (n == 0) {
		errno = err_win_to_posix();
		bb_error_msg_and_die("string conversion failed");
	}
	r.str = xmalloc(n);
	r.need_to_free = true;
	WideCharToMultiByte(bb_codepage, 0, ws, -1, r.str, n, NULL, NULL);
	return r;
}

/*
 * Convert a counted wide string (wlen wchar_t elements, not necessarily
 * NUL-terminated) to a NUL-terminated multibyte string.
 */
mbs_result bb_to_mbs_n(const wchar_t *ws, int wlen, char *buf, int buf_bytes)
{
	mbs_result r;
	int n;

	/* NULL input with zero length: return NULL output */
	if (ws == NULL && wlen == 0) {
		r.str = NULL;
		r.need_to_free = false;
		return r;
	}

	/* Empty input: return an empty NUL-terminated string */
	if (wlen == 0) {
        /* Note: Not setting str = "" because str is mutable. */
		if (buf_bytes > 0) {
			r.str = buf;
			r.need_to_free = false;
		} else {
			r.str = xmalloc(1);
			r.need_to_free = true;
		}
        r.str[0] = '\0';
		return r;
	}

	/*
	 * Optimistic: try to convert into the provided buffer.
	 * Reserve one byte for the NUL terminator.
	 */
	if (buf_bytes > 1) {
		n = WideCharToMultiByte(bb_codepage, 0, ws, wlen, buf, buf_bytes - 1,
				NULL, NULL);
		if (n > 0) {
			buf[n] = '\0';
			r.str = buf;
			r.need_to_free = false;
			return r;
		}
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			errno = err_win_to_posix();
			bb_error_msg_and_die("string conversion failed");
		}
	}

	/* Fallback: query required size, allocate, convert */
	n = WideCharToMultiByte(bb_codepage, 0, ws, wlen, NULL, 0, NULL, NULL);
	if (n == 0) {
		errno = err_win_to_posix();
		bb_error_msg_and_die("string conversion failed");
	}
	r.str = xmalloc((size_t)n + 1);
	r.need_to_free = true;
	WideCharToMultiByte(bb_codepage, 0, ws, wlen, r.str, n, NULL, NULL);
	r.str[n] = '\0';
	return r;
}

void wcs_free(wcs_result *r)
{
	if (r->need_to_free)
		free(r->str);
}

void mbs_free(mbs_result *r)
{
	if (r->need_to_free)
		free(r->str);
}

/*
 * Initialize encoding support: check the BB_CODEPAGE environment variable,
 * convert wide argv to multibyte argv.  Returns a heap-allocated argv array
 * with heap-allocated UTF-8 (or configured codepage) strings.
 */
char **mingw_encoding_init(wchar_t **wargv)
{
	wchar_t cpbuf[16];
	char **argv;
	int argc, i;

	/* Check for a codepage override before converting argv */
	if (GetEnvironmentVariableW(L"BB_CODEPAGE", cpbuf, ARRAY_SIZE(cpbuf))) {
		UINT cp = (UINT)wcstoul(cpbuf, NULL, 10);
		if (cp > 0)
			bb_set_codepage(cp);
	}

	for (argc = 0; wargv[argc] != NULL; argc++)
		continue;

	argv = xmalloc(((size_t)argc + 1) * sizeof(char *));
	for (i = 0; i < argc; i++) {
		char buf[PATH_MAX];
		mbs_result r = bb_to_mbs(wargv[i], buf, sizeof(buf));
		if (r.need_to_free) {
			argv[i] = r.str;
		} else {
			argv[i] = xstrdup(r.str);
		}
	}
	argv[argc] = NULL;
 
    /* getopt() expects __argv to be set. */
	assert(__argv == NULL);
	__argv = argv;

	return argv;
}
