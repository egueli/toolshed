/********************************************************************
 * os9dirrec.c - Recover files from sector containing directory entries
 *
 * $Id$
 ********************************************************************/
#include <util.h>
#include <cocotypes.h>
#include <cocopath.h>
#include <string.h>
#include <math.h>

static char *strcatdup( char *orig, char *cat1, char *cat2 );
static error_code ParseFDSegList(fd_stats *fd, u_int dd_tot, char *path, u_int bps );
static error_code ProcessDirectorySector(os9_path_id os9_path, u_int fd_siz, u_int dd_tot, int dir_lsn, char *path, u_int *count);
static int do_dirrec(char **argv, char *p, int lsn);

#define EFD_OK 0
#define EFD_MOD_YEAR 1
#define EFD_MOD_MONTH 2
#define EFD_MOD_DAY 3
#define EFD_MOD_HOUR 4
#define EFD_MOD_MINUTE 5
#define EFD_MOD_TIME 6
#define EFD_SEGMENT 20
#define EFD_SEGMENT_SIZE 21


/* Help message */
static char const * const helpMessage[] =
{
	"Syntax: dirrec {<disk>} {<lsn>}\n",
	"Usage:  Reads directory entries from an arbitrary sector in a disk\n",
	"image, and tries to recover files.\n",
	NULL
};

static int	gFolderCount,
	gFileCount,
	gPreAllo,		/* Incremented when trying to allocated an already allocated cluster	*/
	gFnotA,			/* Incremented when cluster is used and not in allocattion map			*/
	gAnotF,			/* Incremented when an allocated bit is not in the file structure		*/
	gBadFD; 		/* Incremented when a file descriptor has an out of range file segment  */

typedef struct qCluster_t
{
	struct qCluster_t	*next;
	int					lsn;
} qCluster_t;

typedef struct qBitPath_t
{
	struct qBitPath_t	*next;
	int					lsn;
	char				*path;	
} qBitPath_t;

int os9dirrec(int argc, char *argv[])
{
	error_code	ec = 0;
	char *p = NULL;
	int lsn;
	int i;
	
	/* walk command line for options */
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
		{
			for (p = &argv[i][1]; *p != '\0'; p++)
			{
				switch(*p)
				{
					/* no options currently implemented */	
					default:
						fprintf(stderr, "%s: unknown option '%c'\n", argv[0], *p);
						return(0);
				}
			}
		}
	}

	/* walk command line for pathnames */
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
		{
			continue;
		}
		else
		{
			if (!p)
			{
				p = argv[i];
			}
			else {
				lsn = (int)strtol(argv[i], NULL, 0);
			}
		}
	}

	if (argc < 3 || p == NULL || lsn == 0)
	{
		show_help(helpMessage);
		return(0);
	}

	ec = do_dirrec(argv, p, lsn);

	if (ec != 0)
	{
		fprintf(stderr, "%s: error %d processing '%s'\n", argv[0], ec, p);
		return(ec);
	}

	return(0);
}



static int do_dirrec(char **argv, char *p, int lsn)
{
	error_code	ec = 0;
	os9_path_id		os9_path;
	int		cluster_size;
	char os9pathlist[256];

	if( strchr(p, ',') != 0 )
	{
		fprintf( stderr, "Cannot disk check an OS-9 file, only OS-9 disks.\n" );
		return 1;
	}
	
	gFolderCount = 0;
	gFileCount = 0;
	gPreAllo = 0;
	gFnotA = gAnotF = 0;
	gBadFD = 0;
	
	strcpy(os9pathlist, p);

	/* if the user forgot to add the ',', do it for them */
	if (strchr(os9pathlist, ',') == NULL)
	{
		strcat(os9pathlist, ",.");
	}

	strcat(os9pathlist, "@");

	/* open a path to the device */
	ec = _os9_open(&os9_path, os9pathlist, FAM_READ);
	if (ec != 0)
	{
		fprintf(stderr, "%s: error %d while trying to open '%s'\n", argv[0], ec, os9pathlist);
		return(ec);
	}
	
	cluster_size = int2(os9_path->lsn0->dd_bit);
	
	if( cluster_size == 0 )
	{
		printf("Disk format error: Sectors per cluster cannot be zero.\n" );
		return -1;
	}
	
	int		bps = os9_path->bps;
	u_int	dd_tot = int3(os9_path->lsn0->dd_tot);
	u_int	count = 0;

	ec = ProcessDirectorySector(os9_path, bps, dd_tot, lsn, p, &count);
	
	_os9_close(os9_path);

	return ec; 
}

static error_code SaveFDToFile(os9_path_id os9_path, fd_stats *fd, char *path, u_int bps)
{
	char *filename = strrchr(path, '/');
	if (!filename || strlen(filename) < 2)
	{
		return 1;
	}

	FILE *out = fopen(filename + 1, "wb");
	if (!out)
	{
		return 2;
	}
	
	u_int		remaining = int4(fd->fd_siz);
	u_int 		segment = 0;
	for( ; segment < NUM_SEGS && remaining > 0; segment++)
	{
		u_int segment_lsn = int3(fd->fd_seg[segment].lsn);
		if (segment_lsn == 0)
		{
			break;
		}

		Fd_seg theSeg = &(fd->fd_seg[segment]);
		u_int num = int2(theSeg->num);

		u_char *buffer = (u_char *)malloc( bps );

		for (int i = 0; i <= num && remaining > 0; i++)
		{
			u_int lsn = segment_lsn + i;

			if (read_lsn(os9_path, lsn, buffer) != bps)
			{
				printf("Sector wrong size, terminating\n" );
				exit(-1);
			}

			u_int toWrite = remaining > bps ? bps : remaining;
			size_t written = fwrite(buffer, sizeof buffer[0], toWrite, out);
			if (written != toWrite) {
				printf("Unable to write to file\n");
				exit(-1);
			}

			remaining -= written;
		}

		free(buffer);
	}

	fclose(out);

	if (segment == NUM_SEGS)
	{
		return 3;
	}

	return EFD_OK;
}

static error_code CheckFDFields(fd_stats *file_fd)
{
	u_int mod_year = 1900 + file_fd->fd_dat[0];
	if (mod_year >= 2000) {
		return EFD_MOD_YEAR;
	}
	
	u_int mod_month = file_fd->fd_dat[1];
	if (mod_month > 12) {
		return EFD_MOD_MONTH;
	}

	u_int mod_day = file_fd->fd_dat[2];
	if (mod_day > 31) {
		return EFD_MOD_DAY;
	}

	u_int mod_hour = file_fd->fd_dat[3];
	if (mod_hour > 24) {
		return EFD_MOD_HOUR;
	}

	u_int mod_minute = file_fd->fd_dat[4];
	if (mod_minute > 60) {
		return EFD_MOD_MINUTE;
	}

	struct tm mod_tm;
	mod_tm.tm_year = mod_year - 1900;
	mod_tm.tm_mon = mod_month;
	mod_tm.tm_mday = mod_day;
	mod_tm.tm_hour = mod_hour;
	mod_tm.tm_min = mod_minute;
	mod_tm.tm_sec = 0;
	mod_tm.tm_isdst = -1;

	time_t mod_time = mktime(&mod_tm);
	if (mod_time == -1) {
		return EFD_MOD_TIME;
	}

	return EFD_OK;
}

static error_code CheckFD(os9_path_id os9_path, u_int dd_tot, u_int lsn, char *path)
{
	int			bps = os9_path->bps;
	fd_stats    *file_fd = (fd_stats *)malloc( bps );
	error_code	ec = EFD_OK;

	if( file_fd == NULL )
	{
		printf("Out of memory, terminating (003).\n");
		exit(-1);
	}
	
	if( read_lsn(os9_path, lsn, file_fd ) != bps )
	{
		printf("Sector wrong size, terminating (003).\n" );
		printf("LSN: %d\n", lsn );
		exit(-1);
	}

	ec = CheckFDFields(file_fd);

	if (ec == EFD_OK)
	{
		if ((file_fd->fd_att & FAP_DIR))
		{
			printf("%s is a directory, skipping (TBD)\n", path);
		}
		else
		{
			gFileCount++;
			ec = ParseFDSegList(file_fd, dd_tot, path, bps);
			if (ec == EFD_OK) {
				ec = SaveFDToFile(os9_path, file_fd, path, bps);
				if (ec != 0)
				{
					fprintf(stderr, "Failed to save to file: error code %d\n", ec);
				}
			}
		}
	}
	
	free(file_fd);

	return ec;
}

static error_code ProcessDirectoryEntry(os9_path_id os9_path, os9_dir_entry *dEnt, u_int dd_tot, char *path)
{
	char		*newPath;

	if (dEnt->name[0] == 0)
	{
		return(0);
	}

	OS9StringToCString(dEnt->name);
	
	if (strcmp((char *)dEnt->name, ".") == 0)
	{
		return(0);
	}
	if (strcmp((char *)dEnt->name, "..") == 0)
	{
		return(0);
	}

	newPath = strcatdup(path, "/", (char *)dEnt->name);
	unsigned int lsn = int3(dEnt->lsn);
	if (lsn > dd_tot)
	{
		printf("Direntry %s: contains bad LSN\n", newPath);
		free(newPath);
		return 0;
	}

	error_code result = CheckFD(os9_path, dd_tot, lsn, newPath);
	if (!result)
	{
		printf("Direntry %s (LSN: 0x%x) seems valid\n", newPath, lsn);
	}
	else
	{
		printf("Direntry %s (LSN: 0x%x): invalid FD: error code %d\n", newPath, lsn, result);
	}

	free(newPath);

	return 0;
}

// static error_code ParseDirectoryEntryName(u_char *buffer, u_char *name)
static error_code CheckDirectoryEntryName(u_char *buffer)
{
	/*
	Three possibilities:
	- entry is empty: all 32 bytes are zeros;
	- entry is of a deleted file: first byte is zero, (0..N) nonzero whose
	  last character has the highest bit set, then (29-N) zero bytes, then
	  a valid LSN number i.e. lower than the disk size;
	- entry is of a regular file: (0..N) nonzero bytes whose last character
	  has the highest bit set, then (29-N) zero bytes, then a valid LSN
	  number.
	*/

	u_int b;
	u_int terminated = 0;
	// name[0] = first & 0x7f;

	if (buffer[0] != '\0')
	{
		b = 0;
	}
	else
	{
		b = 1;
		if (buffer[1] == '\0')
		{
			// Empty entry.
			terminated = 1;
		}
		else
		{
			// Deleted file entry.
			terminated = 0;
		}
	}

	// now reading the characters of the file name
	for (; !terminated && b < 29; b++)
	{
		u_char ch = buffer[b];
		if (ch == '\0')
		{
			return 1; // name abrupted (got null byte without a high bit first)
		}
		else
		{
			// name[b] = ch & 0x7f;
			u_int high = (ch & 0x80);
			if (high)
			{
				// name[b + 1] = '\0';
				terminated = 1;
			}
		}
	}

	if (b == 29 && !terminated)
	{
		// 29 ASCII bytes and no termination. We might be
		// reading a text file. Definitely not an RBF directory entry.
		return 2;
	}

	// now reading the padding bytes, they must all be null.
	u_int p;
	for (p = b + 1; p < 29; p++)
	{
		u_char ch = buffer[p];
		if (ch != '\0')
		{
			return 3;
		}
	}

	return 0;
}

error_code CheckValidDirectorySector(os9_path_id os9_path, os9_dir_entry *dEnt, u_int dd_tot)
{
	u_int           k;
	int			    bps = os9_path->bps;
	for (k = 0; k < (bps / sizeof(os9_dir_entry)); k++)
	{
		os9_dir_entry *thisDEnt = &dEnt[k];

		// char name[30]; // 29 bytes + null terminator

		u_char *buffer = (u_char *)thisDEnt;
		// error_code ec = ParseDirectoryEntryName(buffer, name);
		error_code ec = CheckDirectoryEntryName(buffer);

		if (ec != 0)
		{
			return ec;
		}
		
		u_int lsn = int3(buffer + 29);
		if (lsn >= dd_tot)
		{
			return 4;
		}
	}

	return 0;
}

static error_code ProcessDirectorySector(os9_path_id os9_path, u_int fd_siz, u_int dd_tot, int dir_lsn, char *path, u_int *count)
{
	error_code 	    ec = 0;
	int			    bps = os9_path->bps;
	os9_dir_entry	*dEnt;  /* Each entry is 32 bytes long */
	u_int           k;

	dEnt = (os9_dir_entry *)malloc( bps );
	if( dEnt == NULL )
	{
		printf("Out of memory, terminating (002).\n");
		exit(-1);
	}


	if( read_lsn( os9_path, dir_lsn, dEnt ) != bps )
	{					
		printf("Sector wrong size, terminating (002).\nLSN: %d\n", dir_lsn );
		exit(-1);
	}

	ec = CheckValidDirectorySector(os9_path, dEnt, dd_tot);
	if (ec != 0)
	{
		printf("This sector does not look like a directory (error code %d)\n", ec);
	}
	else
	{
		for (k = 0; k < (bps / sizeof(os9_dir_entry)); k++)
		{
			*count += sizeof(os9_dir_entry);
			if (*count > fd_siz)
			{
				break;
			}

			ProcessDirectoryEntry(os9_path, &dEnt[k], dd_tot, path);
		}
	}

	free(dEnt);

	return ec;
}

static error_code ParseFDSegList( fd_stats *fd, u_int dd_tot, char *path, u_int bps )
{
	error_code	ec = EFD_OK;
	u_int  		i = 0;
	Fd_seg		theSeg;
	u_int 		num;

	u_int 		size = int4(fd->fd_siz);
	u_int		min_sectors = (size / bps) + (size % bps != 0); // https://stackoverflow.com/a/14878734/327648

	u_int		seg_sectors = 0;

	while( int3(fd->fd_seg[i].lsn) != 0 )
	{
		if( i > NUM_SEGS )
		{
			i++;
			break;
		}

		theSeg = &(fd->fd_seg[i]);
		num = int2(theSeg->num);
		seg_sectors += num;

		if( (int3(theSeg->lsn) + num) > dd_tot )
		{
			printf("*** Bad FD segment ($%6.6X-$%6.6X) for file: %s (Segement index: %d)\n", int3(theSeg->lsn), int3(theSeg->lsn)+num, path, i );
			ec = EFD_SEGMENT;
		}
		i++;
	}
	
	if (ec == EFD_OK)
	{
		if (seg_sectors < min_sectors)
		{
			ec = EFD_SEGMENT_SIZE;
		}
	}

	return ec;
}

static char *strcatdup( char *orig, char *cat1, char *cat2 )
{
	char	*result;
	
	if( cat2 == NULL )
		result = (char *)malloc( strlen(orig) + strlen(cat1) + 1 );
	else
		result = (char *)malloc( strlen(orig) + strlen(cat1) + strlen(cat2) + 1 );
		
	if( result != NULL )
	{
		strcpy( result, orig );
		strcat( result, cat1 );
		if( cat2 != NULL )
			strcat( result, cat2 );
	}
	
	return result;
}
