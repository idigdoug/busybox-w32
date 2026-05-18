#include "libbb.h"
#include "mingw_encoding.h"

int FAST_FUNC
tcsetattr(int fd, int mode UNUSED_PARAM, const struct termios *t)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	if (!SetConsoleMode(h, t->w_mode)) {
		errno = err_win_to_posix();
		return -1;
	}

	return 0;
}

int FAST_FUNC tcgetattr(int fd, struct termios *t)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	if (!GetConsoleMode(h, &t->w_mode)) {
		errno = err_win_to_posix();
		return -1;
	}

	t->c_cc[VINTR] = 3;	// ctrl-c
	t->c_cc[VEOF] = 4;	// ctrl-d

	if (t->w_mode & ENABLE_ECHO_INPUT)
		t->c_lflag |= ECHO;
	else
		t->c_lflag &= ~ECHO;

	return 0;
}

/*
 * Read a key from the console, returning a Unicode codepoint (for printable
 * characters) or a KEYCODE_* constant (for special keys).  Uses
 * ReadConsoleInputW so that we always get UTF-16 regardless of console CP.
 * Surrogate pairs are assembled into a full codepoint; unpaired surrogates
 * are returned as-is (permissive, like WTF-8).
 */
int64_t FAST_FUNC windows_read_key(int fd, char *buf UNUSED_PARAM, int timeout)
{
	HANDLE cin = GetStdHandle(STD_INPUT_HANDLE);
	INPUT_RECORD record;
	DWORD nevent_out, mode;
	int64_t ret = -1;
	DWORD alt_pressed = FALSE;
	DWORD state;
	/* Pending code unit from a previous iteration.  If in the high
	 * surrogate range, we'll try to pair it with the next input.
	 * Otherwise, it's returned directly. */
	static DWORD pending_code_unit = 0;
	/* Buffer to track recent key-downs for the missing-key-down workaround */
	#define DOWN_BUF_SIZ 8
	static WCHAR downbuf[DOWN_BUF_SIZ];
	static int downbuf_pos = 0;

	if (fd != 0)
		bb_error_msg_and_die("read_key only works on stdin");
	if (cin == INVALID_HANDLE_VALUE)
		return -1;
	GetConsoleMode(cin, &mode);
	SetConsoleMode(cin, 0);

	while (1) {
		DWORD codepoint;

		/* If we have a pending code unit that's NOT a high surrogate,
		 * return it directly. */
		if (pending_code_unit
		 && !(pending_code_unit >= 0xD800 && pending_code_unit <= 0xDBFF)) {
			ret = (int64_t)pending_code_unit;
			pending_code_unit = 0;
			break;
		}

		errno = 0;
		if (timeout > 0) {
			if (WaitForSingleObject(cin, timeout) != WAIT_OBJECT_0)
				goto done;
		}
		if (!ReadConsoleInputW(cin, &record, 1, &nevent_out))
			goto done;

		if (record.EventType != KEY_EVENT)
			continue;

		state = record.Event.KeyEvent.dwControlKeyState;
		codepoint = record.Event.KeyEvent.uChar.UnicodeChar;

		if (!record.Event.KeyEvent.bKeyDown) {
			/* Workaround: cmd.exe console sometimes sends key-up
			 * without a prior key-down for non-ASCII chars.  Track
			 * recent key-downs and change unmatched key-ups to
			 * key-downs.  Limit to non-ASCII to avoid false triggers
			 * (e.g. ENTER-up at process start). */
			if (codepoint > 127) {
				int i, matched = 0;
				for (i = 0; i < DOWN_BUF_SIZ; i++) {
					if (downbuf[i] == codepoint) {
						downbuf[i] = 0;
						matched = 1;
						break;
					}
				}
				if (!matched) {
					/* No prior key-down — treat as key-down */
					record.Event.KeyEvent.bKeyDown = TRUE;
				} else {
					continue; /* normal key-up, ignore */
				}
			} else {
				/* ignore all key up events except Alt */
				if (!(alt_pressed && (state & LEFT_ALT_PRESSED) == 0 &&
						record.Event.KeyEvent.wVirtualKeyCode == VK_MENU))
					continue;
			}
		} else if (codepoint > 127) {
			/* Remember key-down for the up-to-down workaround */
			downbuf[downbuf_pos++ % DOWN_BUF_SIZ] = (WCHAR)codepoint;
		}
		alt_pressed = state & LEFT_ALT_PRESSED;

		if (!codepoint) {
			if (alt_pressed && !(state & ENHANCED_KEY)) {
				/* keys on numeric pad used to enter character codes */
				switch (record.Event.KeyEvent.wVirtualKeyCode) {
				case VK_NUMPAD0: case VK_INSERT:
				case VK_NUMPAD1: case VK_END:
				case VK_NUMPAD2: case VK_DOWN:
				case VK_NUMPAD3: case VK_NEXT:
				case VK_NUMPAD4: case VK_LEFT:
				case VK_NUMPAD5: case VK_CLEAR:
				case VK_NUMPAD6: case VK_RIGHT:
				case VK_NUMPAD7: case VK_HOME:
				case VK_NUMPAD8: case VK_UP:
				case VK_NUMPAD9: case VK_PRIOR:
					continue;
				}
			}

			switch (record.Event.KeyEvent.wVirtualKeyCode) {
			case VK_DELETE: ret = KEYCODE_DELETE; break;
			case VK_INSERT: ret = KEYCODE_INSERT; break;
			case VK_UP: ret = KEYCODE_UP; break;
			case VK_DOWN: ret = KEYCODE_DOWN; break;
			case VK_RIGHT: ret = KEYCODE_RIGHT; break;
			case VK_LEFT: ret = KEYCODE_LEFT; break;
			case VK_HOME: ret = KEYCODE_HOME; break;
			case VK_END: ret = KEYCODE_END; break;
			case VK_PRIOR: ret = KEYCODE_PAGEUP; break;
			case VK_NEXT: ret = KEYCODE_PAGEDOWN; break;
			default:
				alt_pressed = FALSE;
				continue;
			}

			if (state & (RIGHT_ALT_PRESSED|LEFT_ALT_PRESSED))
				ret &= ~0x20;
			if (state & (RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED))
				ret &= ~0x40;
			if (state & SHIFT_PRESSED)
				ret &= ~0x80;
			goto done;
		}

		/* Handle surrogate pairs */
		if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
			/* High surrogate */
			if (pending_code_unit) {
				/* Already had a pending high surrogate — it's unpaired.
				 * Return old one, save new one for pairing attempt. */
				DWORD old = pending_code_unit;
				pending_code_unit = codepoint;
				codepoint = old;
			} else {
				pending_code_unit = codepoint;
				continue;
			}
		} else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
			/* Low surrogate */
			if (pending_code_unit) {
				/* Combine with pending high surrogate */
				codepoint = 0x10000
					+ (((pending_code_unit & 0x3FF)) << 10)
					+ (codepoint & 0x3FF);
				pending_code_unit = 0;
			}
			/* else: unpaired low surrogate, return as-is */
		} else {
			if (pending_code_unit) {
				/* Non-surrogate after a pending high surrogate.
				 * Return high surrogate as-is (permissive),
				 * save this codepoint for next call. */
				DWORD old = pending_code_unit;
				pending_code_unit = codepoint;
				codepoint = old;
			}
		}

		ret = (int64_t)codepoint;
		if (state & (RIGHT_ALT_PRESSED|LEFT_ALT_PRESSED)) {
			switch (codepoint) {
			case '\b': ret = KEYCODE_ALT_BACKSPACE; goto done;
			case 'b': ret = KEYCODE_ALT_LEFT; goto done;
			case 'd': ret = KEYCODE_ALT_D; goto done;
			case 'f': ret = KEYCODE_ALT_RIGHT; goto done;
			}
		}
		break;
	}
 done:
	SetConsoleMode(cin, mode);
	return ret;
}

/*
 * Encode a Unicode codepoint to the configured internal encoding.
 * buf/buf_bytes: output buffer and its size in bytes.
 * Returns the number of bytes written, or 0 on failure.
 */
int FAST_FUNC windows_codepoint_to_mbs(uint32_t codepoint, char *buf, int buf_bytes)
{
	WCHAR wbuf[3];
	int wlen;
	mingw_mbs_result_t r;
	int len;

	if (codepoint > 0xFFFF) {
		/* Encode as UTF-16 surrogate pair */
		DWORD cp = codepoint - 0x10000;
		wbuf[0] = 0xD800 | (cp >> 10);
		wbuf[1] = 0xDC00 | (cp & 0x3FF);
		wlen = 2;
	} else {
		wbuf[0] = (WCHAR)codepoint;
		wlen = 1;
	}

	r = mingw_to_mbs_n(wbuf, wlen, buf, buf_bytes);
	if (r.str == NULL)
		return 0;
	len = strlen(r.str);
	if (r.need_to_free) {
		/* Result was heap-allocated — copy into caller's buffer */
		if (len < buf_bytes) {
			memcpy(buf, r.str, len + 1);
		} else {
			len = 0; /* doesn't fit */
		}
		mingw_mbs_free(&r);
	}
	return len;
}
