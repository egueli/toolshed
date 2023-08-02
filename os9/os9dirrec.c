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
static error_code ParseFDSegList(fd_stats *fd, u_int dd_tot, char *path, unsigned char *secondaryBitmap );
static error_code ProcessDirectorySector(os9_path_id os9_path, u_int fd_siz, u_int dd_tot, int dir_lsn, char *path, u_int *count);
static error_code BuildSecondaryAllocationMap( os9_path_id os9_path, int dir_lsn, char *path, unsigned char *secondaryBitmap );
static error_code CompareAllocationMap( unsigned char *primaryAlloMap, unsigned char *secondaryBitmap, int dd_map, int cluster_size );
static void PathlistsForQuestionableClusters();
static void FreeQuestionableMemory();
static void AddQuestionableCluster( int cluster );
static void AddPathToBit( int lsn, char *path );
static int do_dirrec(char **argv, char *p, int lsn);
static void PathlistsForQuestionableClusters(void);
static void FreeQuestionableMemory(void);


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

static int	sOption, bOption, pOption;	/* Flags for command line options */

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

static qCluster_t	*qCluster;		/* This is an array of clusters that are reported unusual */
static qBitPath_t	*gBitPaths;		/* Every allocated bit has its path (sometimes more that one) */

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
		fprintf(stderr, "%s: error %d opening '%s'\n", argv[0], ec, p);
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
		fprintf(stderr, "%s: error %d opening '%s'\n", argv[0], ec, os9pathlist);
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

static error_code ProcessDirectoryEntry(os9_path_id os9_path, os9_dir_entry *dEnt, u_int dd_tot, char *path, int k)
{
	int			bps = os9_path->bps;
	fd_stats    *file_fd;
	char		*newPath;

	if (dEnt[k].name[0] == 0)
	{
		return(0);
	}

	OS9StringToCString(dEnt[k].name);
	
	if (strcmp((char *)dEnt[k].name, ".") == 0)
	{
		return(0);
	}
	if (strcmp((char *)dEnt[k].name, "..") == 0)
	{
		return(0);
	}

	newPath = strcatdup(path, "/", (char *)dEnt[k].name);
	
	if (int3(dEnt[k].lsn) > dd_tot)
	{
		printf("File: %s, contains bad LSN\n", newPath);
		free(newPath);
		return 0;
	}

	file_fd = (fd_stats *)malloc( bps );
	if( file_fd == NULL )
	{
		printf("Out of memory, terminating (003).\n");
		exit(-1);
	}
	
	if( read_lsn(os9_path, int3(dEnt[k].lsn), file_fd ) != bps )
	{
		printf("Sector wrong size, terminating (003).\n" );
		printf("LSN: %d\n", int3(dEnt[k].lsn) );
		exit(-1);
	}

	/* If actually a file? */
	if ((file_fd->fd_att & FAP_DIR) == 0)
	{
		gFileCount++;
		ParseFDSegList(file_fd, dd_tot, newPath, NULL);
	}
	
	free(file_fd);
	free(newPath);

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

	for (k = 0; k < (bps / sizeof(os9_dir_entry)); k++)
	{
		*count += sizeof(os9_dir_entry);
		if (*count > fd_siz)
		{
			break;
		}

		ec = ProcessDirectoryEntry(os9_path, dEnt, dd_tot, path, k);
		if (ec != 0)
		{
			break;
		}
	}

	free(dEnt);

	return ec;
}

/* This function will drill down into a directory file and fillout a secondary allocation bitmap.
   It is recursive, so whenever a directory is encoundered it will call itself. It will also compare
   the map it creates with the map suppilied that represents the map on disk. */
   
static error_code BuildSecondaryAllocationMap( os9_path_id os9_path, int dir_lsn, char *path, unsigned char *secondaryBitmap )
{
	error_code 	ec = 0;
	fd_stats	*dir_fd;
	u_int		dd_tot,
				fd_siz,
				count,
				i, j;
	Fd_seg		theSeg;
	int			bps = os9_path->bps;

	/* Check if this directory has already been drilled into */

	if ( _os9_ckbit( secondaryBitmap, dir_lsn ) != 0)
	{
		/* Whoops, it is already allocated! */
		printf("Directory %s has a circular reference. Skipping\n", path);
		AddQuestionableCluster(dir_lsn);
		return(0);
	}
	
	/* Allocate directory file descriptor LSN in secondary allocation map */
	
	_os9_allbit(secondaryBitmap, dir_lsn, 1);
	gFolderCount++;
	
	dd_tot = int3(os9_path->lsn0->dd_tot);
	
	dir_fd = (fd_stats *)malloc( bps );
	if( dir_fd == NULL )
	{
		printf("Out of memory, terminating (001).\n");
		exit(-1);
	}
	
	if (read_lsn(os9_path, dir_lsn, dir_fd ) != bps)
	{
		printf("Sector wrong size, terminating (001).\n" );
		printf("LSN: %d\n", dir_lsn );
		exit(-1);
	}

	/* Parse segment list of directory, report any problems */
	ec = ParseFDSegList( dir_fd, dd_tot, path, secondaryBitmap );
	
	if (ec != 0)
	{
		printf("File descriptor for directory %s is bad. Will not open directory file.\n", path);
		ec = 0;
	}
	else
	{
		/* Now open directory file and parse contents */
		fd_siz = int4(dir_fd->fd_siz);
		count = 0;
		i = 0;
		
		while (int3(dir_fd->fd_seg[i].lsn) != 0)
		{
			theSeg = &(dir_fd->fd_seg[i]);
			
			if (i > NUM_SEGS)
			{
				break;
			}
				
			if (count > fd_siz)
			{
				break;
			}
			
			if (int2(theSeg->num) > dd_tot)
			{
				printf("File: %s contains a bad segment (%d > %d)\n", path, int2(theSeg->num), dd_tot );
				gBadFD++;
				break;
			}
			
			for (j = 0; j < int2(theSeg->num); j++ )
			{
				if (count > fd_siz)
					break;
				
				if (int3(theSeg->lsn) + j > dd_tot)
				{
					printf("File: %s, contains bad LSN (%d > %d)\n", path, int3(theSeg->lsn)+j, dd_tot);
					count += 256;
					gBadFD++;
					break;
				}
				
				int dir_lsn = int3(theSeg->lsn)+j;

				ec = ProcessDirectorySector(os9_path, fd_siz, dd_tot, dir_lsn, path, &count);			
				if (ec != 0)
				{
					return ec;
				}
			}
			
			i++;
		}
	}
	
	free(dir_fd);
	
	return(ec);
}

static error_code ParseFDSegList( fd_stats *fd, u_int dd_tot, char *path, unsigned char *secondaryBitmap )
{
	error_code	ec = 0;
	u_int  		i = 0, j, once;
	Fd_seg		theSeg;
	u_int 		num, curLSN;

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
			printf("*** Bad FD segment ($%6.6X-$%6.6X) for file: %s (Segement index: %d)\n", int3(theSeg->lsn), int3(theSeg->lsn)+num, path, i );
			gBadFD++;
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
					printf("*** Bad FD segment ($%6.6X-$%6.6X) for file: %s (Segement index: %d)\n", int3(theSeg->lsn), int3(theSeg->lsn)+num, path, i );
					gBadFD++;
					once = 1;
					ec = 1;
				}
			}
			else
			{
				/* Record path to this bit */
				AddPathToBit( curLSN, path );

				/* Check if bit is already allocated */
				if ( _os9_ckbit( secondaryBitmap, curLSN ) != 0 )
				{
					/* Whoops, it is already allocated! */
					printf("Sector $%6.6X was previously allocated\n", curLSN );
					AddQuestionableCluster( curLSN );
					gPreAllo++;
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

static error_code CompareAllocationMap( unsigned char *primaryAlloMap, unsigned char *secondaryBitmap, int dd_map, int cluster_size )
{
	error_code ec = 0;
	int i, j, LSN;
	
	for(i=0; i< (dd_map / cluster_size); i++ )
	{
		int p, s;
		
		p = _os9_ckbit( primaryAlloMap, i );
		
		for( j=0; j<cluster_size; j++ )
		{
			LSN = i*cluster_size+j;
			
			s = _os9_ckbit( secondaryBitmap, LSN );
			
			if( p != s )
			{
				if( p == 0 )
				{
					printf("Logical sector %d ($%6.6X) of cluster %d ($%6.6X) in file structure but not in allocation map\n", LSN, LSN, i, i );
					AddQuestionableCluster( i );
					gFnotA++;
				}
				
				if( s == 0 )
				{
					if( bOption == 0 )
						printf("Logical sector %d ($%6.6X) of cluster %d ($%6.6X) in allocation map but not in file structure\n", LSN, LSN, i, i );
						
					gAnotF++;
				}
			}
		}
	}

	return(ec);
}

static void PathlistsForQuestionableClusters(void)
{
	qCluster_t	*tmp;
	qBitPath_t	*bitpath, *tmpBP;
	
	while( qCluster != NULL )
	{
		bitpath = gBitPaths;
		
		while( bitpath != NULL )
		{
			if( qCluster->lsn == bitpath->lsn )
				printf("Cluster $%6.6X in path: %s\n", qCluster->lsn, bitpath->path );
			
			bitpath = bitpath->next;
		}

		tmp = qCluster->next;
		free( qCluster );
		qCluster = tmp;
	}
	
	bitpath = gBitPaths;
		
	while( bitpath != NULL )
	{
		free( bitpath->path );
		tmpBP = bitpath->next;
		free( bitpath );
		bitpath = tmpBP;
	}
}

static void FreeQuestionableMemory(void)
{
	qCluster_t	*tmp;
	qBitPath_t	*tmpBP;
	
	while( qCluster != NULL )
	{
		tmp = qCluster->next;
		free( qCluster );
		qCluster = tmp;
	}

	while( gBitPaths != NULL )
	{
		free( gBitPaths->path );
		tmpBP = gBitPaths->next;
		free( gBitPaths );
		gBitPaths = tmpBP;
	}
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

static void AddQuestionableCluster( int cluster )
{
	qCluster_t *curCluster;
	
	curCluster = (qCluster_t *)malloc( sizeof(qCluster_t) );
	if( curCluster == NULL )
		return;
	
	curCluster->lsn = cluster;
	curCluster->next = qCluster;
	qCluster = curCluster;
}

static void AddPathToBit( int lsn, char *path )
{
	qBitPath_t	*bitPath;
	
	bitPath = (qBitPath_t *)malloc( sizeof (qBitPath_t) );
	if( bitPath == NULL )
		return;
	
	bitPath->lsn = lsn;
	bitPath->path = (char *)strdup( path );
	bitPath->next = gBitPaths;
	gBitPaths = bitPath;
}
