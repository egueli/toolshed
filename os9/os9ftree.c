/********************************************************************
 * os9ftree.c - Creates directories and symlinks from the raw files
 * created by fscan.
 *
 * $Id$
 ********************************************************************/
#include <util.h>
#include <cocotypes.h>
#include <cocopath.h>
#include <string.h>
#include <math.h>
#include <glob.h>
#include <fcntl.h>

static int do_ftree(char **argv, char *p);
static error_code ProcessScannedDirectories();
static error_code ProcessRawDirectory(const char* name);

/* Help message */
static char const * const helpMessage[] =
{
	"Syntax: ftree {<disk>}\n",
	"Usage:  Creates directories and symlinks from the raw files\n",
	"in the current directory, created by fscan.\n",
	NULL
};

int os9ftree(int argc, char *argv[])
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

	ec = do_ftree(argv, p);

	if (ec != 0)
	{
		fprintf(stderr, "%s: error %d processing '%s'\n", argv[0], ec, p);
		return(ec);
	}

	return(0);   
}

static int do_ftree(char **argv, char *p)
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

	ec = ProcessScannedDirectories();

    return ec;
}

static error_code ProcessScannedDirectories()
{
    glob_t globbuf;

    glob("*.dir", 0, NULL, &globbuf);

	int i;
	for (i = 0; i < globbuf.gl_pathc; i++)
    {
		ProcessRawDirectory(globbuf.gl_pathv[i]);
    }

    globfree(&globbuf);

	return 0;
}

static error_code ProcessRawDirectory(const char* name)
{
	printf("processing raw directory %s\n", name);
	//int fd = open(globbuf.gl_pathv[0], O_RDONLY);

	return 0;
}