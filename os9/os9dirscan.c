/********************************************************************
 * os9dirscan.c - Scan disk for directory entries sectors
 *
 * $Id$
 ********************************************************************/
#include <util.h>
#include <cocotypes.h>
#include <cocopath.h>
#include <string.h>
#include <math.h>

static error_code ProcessDirectorySector(os9_path_id os9_path, u_int fd_siz, u_int dd_tot, int dir_lsn, char *path, u_int *count);
static int do_dirscan(char **argv, char *p);

/* Help message */
static char const * const helpMessage[] =
{
	"Syntax: dirscan {<disk>}\n",
	"Usage:  Scan disk for directory entries sectors,\n",
	"and print their LSNs and file entries.\n",
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

int os9dirscan(int argc, char *argv[])
{
	error_code	ec = 0;
	char *p = NULL;
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
		}
	}

	if (argc < 2 || p == NULL)
	{
		show_help(helpMessage);
		return(0);
	}

	ec = do_dirscan(argv, p);

	if (ec != 0)
	{
		fprintf(stderr, "%s: error %d processing '%s'\n", argv[0], ec, p);
		return(ec);
	}

	return(0);
}



static int do_dirscan(char **argv, char *p)
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


	u_int lsn;
	for (lsn = 1; lsn < dd_tot; lsn++)
	{
		u_int result = ProcessDirectorySector(os9_path, bps, dd_tot, lsn, p, &count);
		if (result == 0) {
			printf("LSN 0x%x has directory entries\n", lsn);
		}
	}
	
	_os9_close(os9_path);

	return ec; 
}


static error_code ProcessDirectorySector(os9_path_id os9_path, u_int fd_siz, u_int dd_tot, int dir_lsn, char *path, u_int *count)
{
	error_code 	    ec = 0;
	int			    bps = os9_path->bps;
	os9_dir_entry	*dEnt;  /* Each entry is 32 bytes long */

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

	free(dEnt);

	return ec;
}
