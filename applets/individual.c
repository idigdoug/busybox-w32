/* Minimal wrapper to build an individual busybox applet.
 *
 * Copyright 2005 Rob Landley <rob@landley.net
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

const char *applet_name;

#include <stdio.h>
#include <stdlib.h>
#include "usage.h"

#if ENABLE_PLATFORM_MINGW32
#include "strconv.h"

int wmain(int argc, wchar_t **wargv)
{
	char **argv = mingw_encoding_init(wargv);
	applet_name = argv[0];
	return APPLET_main(argc, argv);
}
#else
int main(int argc, char **argv)
{
	applet_name = argv[0];
	return APPLET_main(argc, argv);
}
#endif

void bb_show_usage(void)
{
	fputs_stdout(APPLET_full_usage "\n");
	exit_FAILURE();
}
