/********************************************************************
 * os9fscan.c - Scan disk for any file, incl. directory files and 
 * deleted files
 *
 * $Id$
 ********************************************************************/
#include <util.h>
#include <cocotypes.h>
#include <cocopath.h>
#include <string.h>
#include <math.h>

static int do_fscan(char **argv, char *p);
static error_code ProcessFileDescriptor(os9_path_id os9_path, u_int next_lsn, char *path, unsigned char* secondaryBitmap);
static error_code MarkFileSectors(os9_path_id os9_path, fd_stats *fd, unsigned char* secondaryBitmap, u_int lsn);

/* Help message */
static char const * const helpMessage[] =
{
	"Syntax: fscan {<disk>}\n",
	"Usage:  Scan disk for any file. Files are saved as\n",
    "\"lsnAABBCC\" and the metadata as \"lsnAABBCC.fd\".\n",
	NULL
};

int os9fscan(int argc, char *argv[])
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

	ec = do_fscan(argv, p);

	if (ec != 0)
	{
		fprintf(stderr, "%s: error %d processing '%s'\n", argv[0], ec, p);
		return(ec);
	}

	return(0);   
}

static int do_fscan(char **argv, char *p)
{
	error_code ec = 0;

	if( strchr(p, ',') != 0 )
	{
		fprintf( stderr, "Cannot disk check an OS-9 file, only OS-9 disks.\n" );
		return 1;
	}

	char os9pathlist[256];

	strcpy(os9pathlist, p);

	/* if the user forgot to add the ',', do it for them */
	if (strchr(os9pathlist, ',') == NULL)
	{
		strcat(os9pathlist, ",.");
	}

	strcat(os9pathlist, "@");

	os9_path_id		os9_path;

	/* open a path to the device */
	ec = _os9_open(&os9_path, os9pathlist, FAM_READ);
	if (ec != 0)
	{
		fprintf(stderr, "%s: error %d while trying to open '%s'\n", argv[0], ec, os9pathlist);
		return(ec);
	}

	int cluster_size = int2(os9_path->lsn0->dd_bit);
	
	if( cluster_size == 0 )
	{
		fprintf(stderr, "Disk format error: Sectors per cluster cannot be zero.\n" );
		return -1;
	}
	
	u_int	dd_tot = int3(os9_path->lsn0->dd_tot);

	unsigned char  *secondaryBitmap = (unsigned char *)malloc( (dd_tot+1) / 8 );
	if( secondaryBitmap == NULL )
	{
		printf("Failed to allocate memory for the secondary bitmap.\n");
		return -1;
	}
	memset(secondaryBitmap, 0, (dd_tot + 1) / 8);

	/* Everything before the root directory is surely not a file, so
	   mark it as already scanned */
	_os9_allbit(secondaryBitmap, 0, int3(os9_path->lsn0->dd_dir));

	while (1)
	{
		// Note: _os9_getfreebit allocates the next free bit and returns its index.
		int free_lsn = _os9_getfreebit(secondaryBitmap, dd_tot);
		if (free_lsn == -1) {
			break;
		}
		// printf("processing: 0x%0x\n", free_lsn);
		u_int result = ProcessFileDescriptor(os9_path, (u_int)free_lsn, p, secondaryBitmap);
		if (result == 0) {
			printf("0x%x\n", free_lsn);
		}
		else {
			// printf("not an FD: 0x%x\n", free_lsn);
		}
		// Mark all the other sectors of this cluster as scanned.
		_os9_allbit(secondaryBitmap, free_lsn, cluster_size);
	}
	
	_os9_close(os9_path);

    return ec;
}

static error_code ProcessFileDescriptor(os9_path_id os9_path, u_int lsn, char *path, unsigned char *secondaryBitmap)
{
	error_code 	    ec = 0;
	int			    bps = os9_path->bps;
	u_int			dd_tot = int3(os9_path->lsn0->dd_tot);
	fd_stats		fd;

	if (read_lsn(os9_path, lsn, &fd) != bps)
	{
		printf("Sector wrong size, terminating (002).\nLSN: 0x%06x\n", lsn );
		exit(-1);
	}

	ec = CheckFDFields(&fd);
	if (ec != EFD_OK)
	{
		return ec;
	}

	ec = ParseFDSegList_simple(&fd, dd_tot, path, bps);
	if (ec != EFD_OK)
	{
		return ec;
	}

	char out_file[32];
	int cx = snprintf(out_file, 32, "./lsn_%06x", lsn);
	if (cx <= 0 || cx > 32)
	{
		fprintf(stderr, "unable to make file name\n");
		exit(-1);
	}

	ec = MarkFileSectors(os9_path, &fd, secondaryBitmap, lsn);

	ec = SaveFDToFile(os9_path, &fd, out_file, bps);
	if (ec != EFD_OK)
	{
		fprintf(stderr, "unable to save file %s: error code %d\n", out_file, ec);
		exit(-1);
	}

	return ec;
}

static error_code MarkFileSectors(os9_path_id os9_path, fd_stats *fd, unsigned char* secondaryBitmap, u_int lsn)
{
	error_code	ec = 0;
	u_int  		i = 0, j, once;
	Fd_seg		theSeg;
	u_int 		num, curLSN;
	u_int		dd_tot = int3(os9_path->lsn0->dd_tot);

	while( int3(fd->fd_seg[i].lsn) != 0 )
	{
		if( i > NUM_SEGS )
		{
			i++;
			break;
		}

		theSeg = &(fd->fd_seg[i]);
		num = int2(theSeg->num);

		if( (int3(theSeg->lsn) + num) > dd_tot )
		{
			printf("*** Bad FD segment (0x%06x-0x%06x) for LSN: 0x%06x (Segment index: %d)\n", int3(theSeg->lsn), int3(theSeg->lsn)+num, lsn, i );
			i++;
			continue;
		}

		for(j = 0; j < num; j++)
		{
			once = 0;
			curLSN = int3(theSeg->lsn)+j;
			
			/* check for segment elements out of bounds */
			if( curLSN > dd_tot )
			{
				if( once == 0 )
				{
					printf("*** Bad FD segment (0x%06x-0x%06x) for LSN 0x%06x (Segment index: %d)\n", int3(theSeg->lsn), int3(theSeg->lsn)+num, lsn, i );
					once = 1;
					ec = 1;
				}
			}
			else
			{
				/* Check if bit is already allocated */
				if ( _os9_ckbit( secondaryBitmap, curLSN ) != 0 )
				{
					/* Whoops, it is already allocated! */
					// printf("Sector 0x%06x was previously allocated\n", curLSN );
				}
				else
				{
					/* Allocate bit and move on */
					_os9_allbit( secondaryBitmap, curLSN, 1);
				}
			}
		}
		
		i++;
	}
	
	return ec;
}
