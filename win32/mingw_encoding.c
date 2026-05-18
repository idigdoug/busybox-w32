/* vi: set sw=4 ts=4: */
/*
 * String conversion between multibyte (char) and wide character (wchar_t)
 * strings, using a configurable code page.
 */
#include "libbb.h"
#include "mingw_encoding.h"
#include <assert.h>

static unsigned bb_codepage = CP_UTF8;
static mingw_codepage_category mingw_cp_type = MINGW_CODEPAGE_UTF8;
static unsigned bb_cp_max_charsize = 4; /* MaxCharSize for current codepage */

/* Lead byte table for DBCS: 256-bit bitmap, indexed by byte value */
static unsigned char bb_lead_byte_map[32];

static void set_codepage(unsigned cp)
{
	CPINFO info;

	memset(bb_lead_byte_map, 0, sizeof(bb_lead_byte_map));

	if (cp == CP_UTF8) {
		bb_codepage = cp;
		mingw_cp_type = MINGW_CODEPAGE_UTF8;
		bb_cp_max_charsize = 4;
		return;
	}

	if (!GetCPInfo(cp, &info) || info.MaxCharSize > MINGW_MAX_CHARSIZE) {
		bb_error_msg("codepage %u is not supported, keeping %u",
				cp, bb_codepage);
		return;
	}

	bb_codepage = cp;
	bb_cp_max_charsize = info.MaxCharSize;

	if (info.MaxCharSize == 1) {
		mingw_cp_type = MINGW_CODEPAGE_SBCS;
	} else if (info.MaxCharSize == 2) {
		mingw_cp_type = MINGW_CODEPAGE_DBCS;
		/* Build lead byte bitmap from the LeadByte ranges.
		 * LeadByte is an array of pairs [low, high], terminated by [0,0]. */
		for (int i = 0; i < MAX_LEADBYTES && info.LeadByte[i]; i += 2) {
			for (unsigned c = info.LeadByte[i]; c <= info.LeadByte[i+1]; c++)
				bb_lead_byte_map[c >> 3] |= (1 << (c & 7));
		}
	} else {
		mingw_cp_type = MINGW_CODEPAGE_OTHER;
	}
}

unsigned mingw_get_codepage(void)
{
	return bb_codepage;
}

mingw_codepage_category mingw_get_codepage_category(void)
{
	return mingw_cp_type;
}

unsigned mingw_get_codepage_max_charsize(void)
{
	return bb_cp_max_charsize;
}

/* bb_lead_byte_map is zeroed for non-DBCS codepages, so this is safe to
 * call unconditionally regardless of codepage type. */
bool mingw_is_lead_byte(unsigned char c)
{
	return (bb_lead_byte_map[c >> 3] & (1 << (c & 7))) != 0;
}

/*
 * Convert a NUL-terminated multibyte string to wide characters.
 * Tries the caller-provided buffer first; falls back to heap allocation.
 */
mingw_wcs_result_t mingw_to_wcs(const char *s, wchar_t *buf, int buf_bytes)
{
	mingw_wcs_result_t r;
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
mingw_wcs_result_t mingw_to_wcs_n(const char *s, int slen, wchar_t *buf, int buf_bytes)
{
	mingw_wcs_result_t r;
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
mingw_mbs_result_t mingw_to_mbs(const wchar_t *ws, char *buf, int buf_bytes)
{
	mingw_mbs_result_t r;
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
mingw_mbs_result_t mingw_to_mbs_n(const wchar_t *ws, int wlen, char *buf, int buf_bytes)
{
	mingw_mbs_result_t r;
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

void mingw_wcs_free(mingw_wcs_result_t *r)
{
	if (r->need_to_free)
		free(r->str);
}

void mingw_mbs_free(mingw_mbs_result_t *r)
{
	if (r->need_to_free)
		free(r->str);
}

/*
 * Decode one character from *src using the current codepage.
 * Advances *src past the decoded bytes.
 * Returns the Unicode codepoint, 0 for end-of-string, or (uint32_t)-1 for
 * invalid sequences.
 */
uint32_t mingw_mbrtowc(const char **src)
{
	const unsigned char *s = (const unsigned char *)*src;
	unsigned char c = *s;

	if (c == 0) {
		return 0;
	}

	switch (mingw_cp_type) {
	case MINGW_CODEPAGE_UTF8: {
		/* Fast inline UTF-8 decoder */
		uint32_t v;
		int bytes;

		if (c <= 0x7f) {
			*src = (const char *)s + 1;
			return c;
		}
		if ((c & 0xe0) == 0xc0) {
			bytes = 2; v = c & 0x1f;
		} else if ((c & 0xf0) == 0xe0) {
			bytes = 3; v = c & 0x0f;
		} else if ((c & 0xf8) == 0xf0) {
			bytes = 4; v = c & 0x07;
		} else {
			/* Invalid lead byte */
			*src = (const char *)s + 1;
			return (uint32_t)-1;
		}
		for (int i = 1; i < bytes; i++) {
			if ((s[i] & 0xc0) != 0x80) {
				/* Truncated sequence */
				*src = (const char *)s + 1;
				return (uint32_t)-1;
			}
			v = (v << 6) | (s[i] & 0x3f);
		}
		/* Reject overlong encodings */
		if (v <= 0x7f || (bytes == 3 && v <= 0x7ff) ||
		    (bytes == 4 && v <= 0xffff)) {
			*src = (const char *)s + 1;
			return (uint32_t)-1;
		}
		*src = (const char *)s + bytes;
		return v;
	}

	case MINGW_CODEPAGE_SBCS: {
		/* Single byte: use MultiByteToWideChar for the mapping */
		wchar_t wc;
		char mb[1] = { (char)c };
		if (MultiByteToWideChar(bb_codepage, 0, mb, 1, &wc, 1) == 1) {
			*src = (const char *)s + 1;
			return (uint32_t)wc;
		}
		*src = (const char *)s + 1;
		return (uint32_t)-1;
	}

	case MINGW_CODEPAGE_DBCS: {
		/* Check if lead byte; if so, consume two bytes */
		int len = mingw_is_lead_byte(c) ? 2 : 1;
		wchar_t wc;
		if (s[0] && (len == 1 || s[1])) {
			if (MultiByteToWideChar(bb_codepage, 0, (const char *)s, len, &wc, 1) == 1) {
				*src = (const char *)s + len;
				return (uint32_t)wc;
			}
		}
		*src = (const char *)s + 1;
		return (uint32_t)-1;
	}

	default: /* MINGW_CODEPAGE_OTHER */
	{
		/* Try 1..max_charsize bytes until we get a valid conversion */
		wchar_t wbuf[2]; /* may produce surrogate pair */
		for (unsigned len = 1; len <= bb_cp_max_charsize; len++) {
			if (s[len - 1] == 0 && len > 1)
				break; /* don't read past NUL */
			int rc = MultiByteToWideChar(bb_codepage, MB_ERR_INVALID_CHARS,
					(const char *)s, len, wbuf, 2);
			if (rc >= 1) {
				uint32_t cp = wbuf[0];
				/* Handle surrogate pair */
				if (rc == 2 && (wbuf[0] & 0xFC00) == 0xD800) {
					cp = 0x10000 + ((wbuf[0] & 0x3FF) << 10)
					             + (wbuf[1] & 0x3FF);
				}
				*src = (const char *)s + len;
				return cp;
			}
		}
		*src = (const char *)s + 1;
		return (uint32_t)-1;
	}
	}
}

/*
 * Encode a Unicode codepoint to multibyte using the current codepage.
 * Returns the number of bytes written to buf.
 * Returns 0 on failure (codepoint cannot be represented).
 */
int mingw_wcrtomb(char *buf, uint32_t codepoint)
{
	if (codepoint == 0) {
		buf[0] = '\0';
		return 1;
	}

	if (mingw_cp_type == MINGW_CODEPAGE_UTF8) {
		/* Fast inline UTF-8 encoder */
		if (codepoint <= 0x7f) {
			buf[0] = (char)codepoint;
			return 1;
		}
		if (codepoint <= 0x7ff) {
			buf[0] = 0xc0 | (codepoint >> 6);
			buf[1] = 0x80 | (codepoint & 0x3f);
			return 2;
		}
		if (codepoint <= 0xffff) {
			buf[0] = 0xe0 | (codepoint >> 12);
			buf[1] = 0x80 | ((codepoint >> 6) & 0x3f);
			buf[2] = 0x80 | (codepoint & 0x3f);
			return 3;
		}
		if (codepoint <= 0x10ffff) {
			buf[0] = 0xf0 | (codepoint >> 18);
			buf[1] = 0x80 | ((codepoint >> 12) & 0x3f);
			buf[2] = 0x80 | ((codepoint >> 6) & 0x3f);
			buf[3] = 0x80 | (codepoint & 0x3f);
			return 4;
		}
		return 0;
	}

	/* For SBCS/DBCS/OTHER: convert via WideCharToMultiByte */
	wchar_t wbuf[2];
	int wlen;
	if (codepoint <= 0xffff) {
		wbuf[0] = (wchar_t)codepoint;
		wlen = 1;
	} else if (codepoint <= 0x10ffff) {
		/* Encode as surrogate pair */
		codepoint -= 0x10000;
		wbuf[0] = 0xD800 | (codepoint >> 10);
		wbuf[1] = 0xDC00 | (codepoint & 0x3FF);
		wlen = 2;
	} else {
		return 0;
	}

	int rc = WideCharToMultiByte(bb_codepage, 0, wbuf, wlen,
			buf, 6, NULL, NULL);
	return rc > 0 ? rc : 0;
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
		unsigned cp = (unsigned)wcstoul(cpbuf, NULL, 10);
		if (cp > 0)
			set_codepage(cp);
	}

	for (argc = 0; wargv[argc] != NULL; argc++)
		continue;

	argv = xmalloc(((size_t)argc + 1) * sizeof(char *));
	for (i = 0; i < argc; i++) {
		char buf[PATH_MAX];
		mingw_mbs_result_t r = mingw_to_mbs(wargv[i], buf, sizeof(buf));
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
