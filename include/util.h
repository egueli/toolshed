/********************************************************************
 * util.h - Utility header file
 *
 * $Id$
 ********************************************************************/

#ifndef _UTIL_H
#define _UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <cocotypes.h>
#include <os9path.h>

/* Function prototypes for supported OS-9 commands are here */
int os9attr(int, char **);
int os9cmp(int, char **);
int os9copy(int, char **);
int os9dcheck(int, char **);
int os9del(int, char **);
int os9deldir(int, char **);
int os9dir(int, char **);
int os9dirrec(int, char **);
int os9dirscan(int, char **);
int os9dsave(int, char **);
int os9dump(int, char **);
int os9format(int, char **);
int os9free(int, char **);
int os9ftree(int, char **);
int os9fscan(int, char **);
int os9fstat(int, char **);
int os9gen(int, char **);
int os9id(int, char **);
int os9ident(int, char **);
int os9list(int, char **);
int os9makdir(int, char **);
int os9modbust(int, char **);
int os9padrom(int, char **);
int os9rename(int, char **);
int os9rdump(int, char **);

/* Function prototypes for OS-9 common functions */
error_code CheckValidDirectorySector(os9_path_id os9_path, os9_dir_entry *dEnt, u_int dd_tot);

#define EFD_OK 0
#define EFD_MOD_YEAR 1
#define EFD_MOD_MONTH 2
#define EFD_MOD_DAY 3
#define EFD_MOD_HOUR 4
#define EFD_MOD_MINUTE 5
#define EFD_MOD_TIME 6
#define EFD_SEGMENT 7
#define EFD_ATTR 8
#define EFD_SEGMENT_SIZE 21
error_code CheckFDFields(fd_stats *file_fd);
error_code ParseFDSegList_simple( fd_stats *fd, u_int dd_tot, char *path, u_int bps );
error_code SaveFDToFile(os9_path_id os9_path, fd_stats *fd, char *path, u_int bps);


int StrToInt(char *s);
#ifdef BDS
int strcasecmp(char *s1, char *s2);
int strncasecmp(char *s1, char *s2, int len);
#endif
int strendcasecmp( char *s1, char *s2 );
void show_help(char const * const *helpMessage);

/* Function prototypes for supported Disk BASIC commands are here */
int decbattr(int, char **);
int decbcopy(int, char **);
int decbdir(int, char **);
int decbdskini(int, char **);
int decbfree(int, char **);
int decbfstat(int, char **);
int decbkill(int, char **);
int decblist(int, char **);
int decbrename(int, char **);
int decbdump(int, char **);
int decbhdbconv(int, char **);
int decbdsave(int, char**);

/* Function prototypes for supported Disk BASIC commands are here */
int cecbdir(int, char **);
int cecbfstat(int, char **);
int cecbbulkerase(int, char **);
int cecbcopy(int, char **);

#ifdef __cplusplus
}
#endif

#endif	/* _UTIL_H */
