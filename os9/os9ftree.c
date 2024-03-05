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
#include <sys/stat.h>

static int do_ftree(char **argv, char *p);
static error_code ProcessScannedDirectories(os9_path_id os9_path);
static error_code ProcessRawDirectory(os9_path_id os9_path, const char* name);
static error_code ReadFileEntirely(const char* path, u_char** outBuffer, size_t *outSize);

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

	ec = ProcessScannedDirectories(os9_path);

    return ec;
}

static error_code ProcessScannedDirectories(os9_path_id os9_path)
{
    glob_t globbuf;

    glob("*.dir", 0, NULL, &globbuf);

	int i;
	for (i = 0; i < globbuf.gl_pathc; i++)
    {
		ProcessRawDirectory(os9_path, globbuf.gl_pathv[i]);
    }

    globfree(&globbuf);

	return 0;
}

static error_code ProcessRawDirectory(os9_path_id os9_path, const char* name)
{
	int ec;

	printf("processing raw directory %s\n", name);
	
	size_t size;
	u_char *dir_data;
	ec = ReadFileEntirely(name, &dir_data, &size);
	if (ec) {
		fprintf(stderr, "Unable to read raw directory file %s: error code %d\n", name, ec);
		return 1;
	}

	int k;
	for (k = 0; k < (size / sizeof(os9_dir_entry)); k++)
	{
		os9_dir_entry *thisDEnt = ((os9_dir_entry*) dir_data) + k;
		OS9StringToCString(thisDEnt->name);
		printf("there's a directory entry %s\n", thisDEnt->name);
		//ProcessDirectoryEntry(os9_path, &dEnt[k], dd_tot, path);
		// TODO discard . and ..
		// TODO handle deleted file
	}

	free(dir_data);

	return 0;
}


static error_code ReadFileEntirely(const char* path, u_char** outBuffer, size_t *outSize)
{
	struct stat stats;
	error_code ec = stat(path, &stats);
	if (ec) {
		fprintf(stderr, "Unable to get stats for %s\n", path);
		return 1;
	} 

	off_t size = stats.st_size;

	u_char *dir_data = malloc(size);
	if (!dir_data) {
		fprintf(stderr, "unable to allocate %ld bytes for directory data\n", size);
		exit(-1);
	}


	FILE* file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "Unable to open file %s\n", path);
		free(dir_data);
		return 1;
	}

	u_char *next_chunk = dir_data;
	size_t remaining = size;
	while(remaining > 0) {
		size_t chunk_size = (remaining < 1024) ? remaining : 1024;
		size_t read_count = fread(next_chunk, sizeof(u_char), chunk_size, file);
		if (read_count < 0)
		{
			fprintf(stderr, "Failed to read from %s at offset 0x%lx\n", path, (next_chunk - dir_data));
			free(dir_data);
			fclose(file);
			return 1;
		}
		remaining -= read_count;
		next_chunk += read_count;
	}

	fclose(file);

	*outBuffer = dir_data;
	*outSize = size;

	return 0;
}