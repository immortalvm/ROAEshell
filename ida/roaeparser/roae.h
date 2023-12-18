/*
    libroae - Simple management of ROAE files 

    Immortal Database Access (iDA) EUROSTARS project

    Eladio Gutierrez, Sergio Romero, Oscar Plata
    University of Malaga, Spain

    Aug 2023
*/

#ifndef __IDA_ROAE_H__
#define __IDA_ROAE_H__

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
#include <ctype.h>

#define MAXFILENAME (PATH_MAX)

long IDA_ROAE_load(char *filename);
void IDA_ROAE_clear();
void IDA_ROAE_print_commands();
void IDA_ROAE_print_command(long nc);
void IDA_ROAE_search(char *re);
long IDA_ROAE_count();
char* IDA_ROAE_eval_command(long nc, char *buff, long buffsize, char *values[]);

#endif /* ! __IDA_ROAE_H__ */
