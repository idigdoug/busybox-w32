#include "libbb.h"
#include "strconv.h"

/*
 * Environment subsystem.  We maintain a single char** array (environ_buf)
 * as the authoritative store.  Each entry is a separately malloc'd
 * "NAME=VALUE" string.  getenv() returns a pointer into that string
 * (past the '='), so pointers remain stable until the variable is
 * modified or removed.
 *
 * setenv/putenv/unsetenv also call SetEnvironmentVariableW so the
 * OS process environment stays in sync (for child process inheritance
 * and for GetEnvironmentStringsW callers like spawnveq).
 */

static char **environ_buf;   /* NULL-terminated array of "NAME=VALUE" */
static int environ_count;    /* number of entries (excluding NULL) */
static int environ_alloc;    /* allocated slots (excluding NULL) */
static bool environ_inited;

/*
 * Find the index of a variable by name (case-insensitive).
 * Returns -1 if not found.
 */
static int env_find_index(const char *name)
{
	int i;
	size_t len = strlen(name);

	for (i = 0; i < environ_count; i++) {
		if (strncasecmp(environ_buf[i], name, len) == 0
		    && environ_buf[i][len] == '=')
			return i;
	}
	return -1;
}

/*
 * Sync a variable to the OS environment via SetEnvironmentVariableW,
 * and also to the CRT environment via _wputenv (so CRT functions like
 * _tzset and localtime see the updated value).
 * value==NULL means delete.
 */
static void env_sync_to_os(const char *name, const char *value)
{
	char *envstr = xasprintf("%s=%s", name, value ? value : "");
	wchar_t wbuf[512];
	wcs_result wr = bb_to_wcs(envstr, wbuf, sizeof(wbuf));
	wchar_t *weq;

	/* Push "NAME=VALUE" to CRT */
	_wputenv(wr.str);

	/* Split at '=' and push to OS */
	weq = wcschr(wr.str, L'=');
	*weq = L'\0';
	SetEnvironmentVariableW(wr.str, value ? weq + 1 : NULL);

	wcs_free(&wr);
	free(envstr);
}

/*
 * Initialize the environ array from the OS environment block.
 * Called lazily on first access.
 */
static void env_init(void)
{
	wchar_t *env_block, *p;
	int count, i;

	if (environ_inited)
		return;
	environ_inited = true;

	env_block = GetEnvironmentStringsW();
	if (!env_block) {
		environ_alloc = 16;
		environ_buf = xzalloc(((size_t)environ_alloc + 1) * sizeof(char *));
		return;
	}

	count = 0;
	for (p = env_block; *p; p += wcslen(p) + 1)
		count++;

	environ_alloc = count + 16;
	environ_buf = xmalloc(((size_t)environ_alloc + 1) * sizeof(char *));
	environ_count = count;

	i = 0;
	for (p = env_block; *p; p += wcslen(p) + 1) {
		char buf[PATH_MAX];
		mbs_result r = bb_to_mbs(p, buf, sizeof(buf));
		environ_buf[i++] = r.need_to_free ? r.str : xstrdup(r.str);
	}
	environ_buf[count] = NULL;

	FreeEnvironmentStringsW(env_block);
}

/*
 * Ensure there is room for at least one more entry.
 */
static void env_grow(void)
{
	if (environ_count >= environ_alloc) {
		environ_alloc = environ_alloc ? environ_alloc * 2 : 16;
		environ_buf = xrealloc(environ_buf,
			((size_t)environ_alloc + 1) * sizeof(char *));
	}
}

char * FAST_FUNC mingw_getenv(const char *name, bool check_fallbacks)
{
	int idx;

	if (!name)
		return NULL;

	env_init();

	idx = env_find_index(name);
	if (idx >= 0) {
		/* Return pointer past the '=' */
		return environ_buf[idx] + strlen(name) + 1;
	}

	if (check_fallbacks) {
		if (!strcmp(name, "TMPDIR")) {
			char *r = mingw_getenv("TMP", false);
			if (!r)
				r = mingw_getenv("TEMP", false);
			return r;
		} else if (!strcmp(name, "HOME")) {
			struct passwd *p = getpwuid(getuid());
			if (p)
				return p->pw_dir;
		}
	}
	return NULL;
}

int FAST_FUNC mingw_putenv(const char *env)
{
	char *s;

	s = strchr(env, '=');
	if (s == NULL)
		return unsetenv(env);

	{
		char *name = xstrndup(env, s - env);
		const char *value = s + 1;
		int ret = setenv(name, value, 1);
		free(name);
		return ret;
	}
}

int FAST_FUNC setenv(const char *name, const char *value, int replace)
{
	int idx;

	if (!name || !*name || strchr(name, '='))
		return -1;

	env_init();

	idx = env_find_index(name);
	if (idx >= 0) {
		if (!replace)
			return 0;
		free(environ_buf[idx]);
		environ_buf[idx] = xasprintf("%s=%s", name, value ? value : "");
	} else {
		env_grow();
		environ_buf[environ_count] = xasprintf("%s=%s", name, value ? value : "");
		environ_count++;
		environ_buf[environ_count] = NULL;
	}

	env_sync_to_os(name, value ? value : "");
	return 0;
}

int FAST_FUNC unsetenv(const char *name)
{
	int idx;

	if (!name || !*name || strchr(name, '='))
		return -1;

	env_init();

	idx = env_find_index(name);
	if (idx >= 0) {
		free(environ_buf[idx]);
		/* Shift remaining entries down */
		environ_count--;
		memmove(&environ_buf[idx], &environ_buf[idx + 1],
			((size_t)environ_count - idx + 1) * sizeof(char *));
	}

	env_sync_to_os(name, NULL);
	return 0;
}

int clearenv(void)
{
	int i;

	env_init();

	/* Clear OS environment */
	for (i = 0; i < environ_count; i++) {
		char *eq = strchr(environ_buf[i], '=');
		if (eq) {
			char *name = xstrndup(environ_buf[i], eq - environ_buf[i]);
			env_sync_to_os(name, NULL);
			free(name);
		}
		free(environ_buf[i]);
	}
	environ_count = 0;
	environ_buf[0] = NULL;
	return 0;
}

char **mingw_environ(void)
{
	env_init();
	return environ_buf;
}
