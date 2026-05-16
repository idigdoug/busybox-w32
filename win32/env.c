#include "libbb.h"
#include "strconv.h"

/*
 * Environment variable cache.  We bypass the CRT's environment entirely
 * and use the Windows environment block (Get/SetEnvironmentVariableW).
 * Returned strings are cached in a linked list so that pointers from
 * mingw_getenv() remain valid until the variable is changed or unset.
 */

struct env_entry {
	struct env_entry *next;
	char *name;
	char *value;
};

static struct env_entry *env_cache;

/* Cached environ array, rebuilt lazily when dirty */
static char **environ_buf;
static int environ_count;
static bool environ_dirty = true;

/*
 * Find a cache entry by name (case-insensitive, matching Windows behavior).
 */
static struct env_entry *env_find(const char *name)
{
	struct env_entry *e;

	for (e = env_cache; e != NULL; e = e->next) {
		if (strcasecmp(e->name, name) == 0)
			return e;
	}
	return NULL;
}

/*
 * Remove and free a cache entry.
 */
static void env_remove(const char *name)
{
	struct env_entry **pp;

	for (pp = &env_cache; *pp != NULL; pp = &(*pp)->next) {
		if (strcasecmp((*pp)->name, name) == 0) {
			struct env_entry *e = *pp;
			*pp = e->next;
			free(e->name);
			free(e->value);
			free(e);
			return;
		}
	}
}

char * FAST_FUNC mingw_getenv(const char *name, bool check_fallbacks)
{
	wchar_t wname_buf[256];
	wchar_t wval_buf[256];
	wcs_result wname;
	struct env_entry *e;
	DWORD n;

	if (!name)
		return NULL;

	wname = bb_to_wcs(name, wname_buf, sizeof(wname_buf));
	n = GetEnvironmentVariableW(wname.str, wval_buf, ARRAY_SIZE(wval_buf));
	if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
		wcs_free(&wname);
		if (check_fallbacks) {
			/* Fallbacks for POSIX names */
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

	if (n < ARRAY_SIZE(wval_buf)) {
		/* Value fit in the stack buffer */
		char mbuf[PATH_MAX];
		mbs_result mr = bb_to_mbs(wval_buf, mbuf, sizeof(mbuf));

		e = env_find(name);
		if (!e) {
			e = xzalloc(sizeof(*e));
			e->name = xstrdup(name);
			e->next = env_cache;
			env_cache = e;
		} else {
			free(e->value);
		}
		e->value = mr.need_to_free ? mr.str : xstrdup(mr.str);
	} else {
		/* Need a larger buffer */
		wchar_t *wval = xmalloc((size_t)(n + 1) * sizeof(wchar_t));
		char mbuf[PATH_MAX];
		mbs_result mr;

		GetEnvironmentVariableW(wname.str, wval, n + 1);
		mr = bb_to_mbs(wval, mbuf, sizeof(mbuf));
		free(wval);

		e = env_find(name);
		if (!e) {
			e = xzalloc(sizeof(*e));
			e->name = xstrdup(name);
			e->next = env_cache;
			env_cache = e;
		} else {
			free(e->value);
		}
		e->value = mr.need_to_free ? mr.str : xstrdup(mr.str);
	}

	wcs_free(&wname);
	return e->value;
}

int FAST_FUNC mingw_putenv(const char *env)
{
	char *s;

	s = strchr(env, '=');
	if (s == NULL) {
		return unsetenv(env);
	}

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
	wchar_t wname_buf[256];
	wchar_t wval_buf[256];
	wcs_result wname, wval;
	struct env_entry *e;

	if (!name || !*name || strchr(name, '='))
		return -1;

	if (!replace && mingw_getenv(name, false))
		return 0;

	wname = bb_to_wcs(name, wname_buf, sizeof(wname_buf));
	wval = bb_to_wcs(value ? value : "", wval_buf, sizeof(wval_buf));

	if (!SetEnvironmentVariableW(wname.str, wval.str)) {
		wcs_free(&wname);
		wcs_free(&wval);
		errno = err_win_to_posix();
		return -1;
	}

	wcs_free(&wname);
	wcs_free(&wval);

	/* Update cache */
	e = env_find(name);
	if (!e) {
		e = xzalloc(sizeof(*e));
		e->name = xstrdup(name);
		e->next = env_cache;
		env_cache = e;
	} else {
		free(e->value);
	}
	e->value = xstrdup(value ? value : "");
	environ_dirty = true;
	return 0;
}

int FAST_FUNC unsetenv(const char *name)
{
	wchar_t wname_buf[256];
	wcs_result wname;

	if (!name || !*name || strchr(name, '='))
		return -1;

	wname = bb_to_wcs(name, wname_buf, sizeof(wname_buf));
	SetEnvironmentVariableW(wname.str, NULL);
	wcs_free(&wname);

	env_remove(name);
	environ_dirty = true;
	return 0;
}

int clearenv(void)
{
	wchar_t *env_block = GetEnvironmentStringsW();
	wchar_t *p;

	if (!env_block)
		return -1;

	for (p = env_block; *p; p += wcslen(p) + 1) {
		wchar_t *eq = wcschr(p, L'=');
		if (eq && eq != p) {
			/* Temporarily NUL-terminate to get the name */
			*eq = L'\0';
			SetEnvironmentVariableW(p, NULL);
			*eq = L'=';
		}
	}
	FreeEnvironmentStringsW(env_block);

	/* Free the entire cache */
	while (env_cache) {
		struct env_entry *e = env_cache;
		env_cache = e->next;
		free(e->name);
		free(e->value);
		free(e);
	}

	environ_dirty = true;
	return 0;
}

/*
 * Build a wide-character environment block suitable for passing to
 * CreateProcessW.  Returns a heap-allocated block; caller must free().
 * The block is a sequence of "NAME=VALUE\0" strings, terminated by
 * an extra NUL.
 */
wchar_t *mingw_env_block(void)
{
	wchar_t *env_block = GetEnvironmentStringsW();
	wchar_t *p;
	size_t total;

	if (!env_block)
		return NULL;

	/* Compute total size including final NUL */
	total = 0;
	for (p = env_block; *p; p += wcslen(p) + 1)
		total += wcslen(p) + 1;
	total++;  /* trailing NUL */

	/* Make a copy the caller can free */
	{
		wchar_t *copy = xmalloc(total * sizeof(wchar_t));
		memcpy(copy, env_block, total * sizeof(wchar_t));
		FreeEnvironmentStringsW(env_block);
		return copy;
	}
}

/*
 * Free the cached environ array contents.
 */
static void environ_free(void)
{
	int i;

	if (!environ_buf)
		return;
	for (i = 0; i < environ_count; i++)
		free(environ_buf[i]);
	free(environ_buf);
	environ_buf = NULL;
	environ_count = 0;
}

/*
 * Return a char** environ array built from the OS environment.
 * The array is cached and only rebuilt when the environment has been
 * modified through our setenv/unsetenv/clearenv/putenv functions.
 */
char **mingw_environ(void)
{
	wchar_t *env_block, *p;
	int count, i;

	if (!environ_dirty)
		return environ_buf;

	environ_free();

	env_block = GetEnvironmentStringsW();
	if (!env_block) {
		environ_buf = xzalloc(sizeof(char *));
		environ_dirty = false;
		return environ_buf;
	}

	/* Count entries */
	count = 0;
	for (p = env_block; *p; p += wcslen(p) + 1)
		count++;

	environ_buf = xmalloc(((size_t)count + 1) * sizeof(char *));
	environ_count = count;

	i = 0;
	for (p = env_block; *p; p += wcslen(p) + 1) {
		char buf[PATH_MAX];
		mbs_result r = bb_to_mbs(p, buf, sizeof(buf));
		if (r.need_to_free) {
			environ_buf[i] = r.str;
		} else {
			environ_buf[i] = xstrdup(r.str);
		}
		i++;
	}
	environ_buf[count] = NULL;

	FreeEnvironmentStringsW(env_block);
	environ_dirty = false;
	return environ_buf;
}
