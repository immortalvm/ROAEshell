/*
 * Mini grep implementation for busybox
 *
 *
 * Copyright (C) 1999 by Lineo, inc.
 * Written by Erik Andersen <andersen@lineo.com>, <andersee@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

//#include "internal.h"

/* Some useful definitions */
#define FALSE   ((int) 1)
#define TRUE    ((int) 0)

#define PATH_LEN        1024
#define BUF_SIZE        8192
#define EXPAND_ALLOC    1024

#define isBlank(ch)     (((ch) == ' ') || ((ch) == '\t'))
#define isDecimal(ch)   (((ch) >= '0') && ((ch) <= '9'))
#define isOctal(ch)     (((ch) >= '0') && ((ch) <= '7'))
#define isWildCard(ch)  (((ch) == '*') || ((ch) == '?') || ((ch) == '['))


//#include "regexp.h"
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

#define usage(x) puts(x)

static const char grep_usage[] =
"grep [OPTIONS]... PATTERN [FILE]...\n\n"
"Search for PATTERN in each FILE or standard input.\n\n"
"OPTIONS:\n"
"\t-h\tsuppress the prefixing filename on output\n"
"\t-i\tignore case distinctions\n"
"\t-n\tprint line number with output lines\n\n"
#if defined BB_REGEXP
"This version of grep matches full regular expresions.\n";
#else
"This version of grep matches strings (not regular expresions).\n";
#endif

extern int find_match(char *haystack, char *needle, int ignoreCase);
static void do_grep(FILE *fp, char* needle, char *fileName, int tellName, int ignoreCase, int tellLine)
{
    char *cp;
    long line = 0;
    char haystack[BUF_SIZE];

    while (fgets (haystack, sizeof (haystack), fp)) {
	line++;
	cp = &haystack[strlen (haystack) - 1];

	if (*cp != '\n')
	    fprintf (stderr, "%s: Line too long\n", fileName);

	if (find_match(haystack, needle, ignoreCase) == TRUE) {
	    if (tellName==TRUE)
		printf ("%s:", fileName);

	    if (tellLine==TRUE)
		printf ("%ld:", line);

	    fputs (haystack, stdout);
	}
    }
}


int main_grep (int argc, char **argv)
{
    FILE *fp;
    char *cp;
    char *needle;
    char *fileName;
    int tellName=FALSE;
    int ignoreCase=FALSE;
    int tellLine=FALSE;


    ignoreCase = FALSE;
    tellLine = FALSE;

    argc--;
    argv++;
    if (argc < 1) {
	usage(grep_usage);
	return -1;
    }

    if (**argv == '-') {
	argc--;
	cp = *argv++;

	while (*++cp)
	    switch (*cp) {
	    case 'i':
		ignoreCase = TRUE;
		break;

	    case 'h':
		tellName = TRUE;
		break;

	    case 'n':
		tellLine = TRUE;
		break;

	    default:
		usage(grep_usage);
	    }
    }

    needle = *argv++;
    argc--;

    if (argc==0) {
	do_grep( stdin, needle, "stdin", FALSE, ignoreCase, tellLine);
    } else {
	while (argc-- > 0) {
	    fileName = *argv++;

	    fp = fopen (fileName, "r");
	    if (fp == NULL) {
		perror (fileName);
		continue;
	    }

	    do_grep( fp, needle, fileName, tellName, ignoreCase, tellLine);

	    if (ferror (fp))
		perror (fileName);
	    fclose (fp);
	}
    }
    return 0;
}


/* END CODE */

