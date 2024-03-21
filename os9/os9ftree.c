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
#include <errno.h>
#include <unistd.h>

static int do_ftree(char **argv, char *p);
static error_code ProcessScannedDirectories(os9_path_id os9_path);
static error_code ProcessRawDirectory(os9_path_id os9_path, const char* name);
static error_code ReadFileEntirely(const char* path, u_char** outBuffer, size_t *outSize);
static error_code ProcessDirectoryEntry(os9_path_id os9_path, const char* dirname, const char* name, const u_int lsn);

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

	ec = mkdir("dirs", 0777);
	if (ec != 0) {
		fprintf(stderr, "unable to create output directory: %s\n", strerror(errno));
		return -1;
	}

	ec = ProcessScannedDirectories(os9_path);

    return ec;
}

static error_code ProcessScannedDirectories(os9_path_id os9_path)
{
	error_code ec;

    glob_t globbuf;

    glob("*.dir", 0, NULL, &globbuf);

	int i;
	for (i = 0; i < globbuf.gl_pathc; i++)
    {
		ec = ProcessRawDirectory(os9_path, globbuf.gl_pathv[i]);
		if (ec) {
			printf("error processing raw directory %s, exiting.\n", globbuf.gl_pathv[i]);
			break;	
		}
    }

    globfree(&globbuf);

	return ec;
}

static error_code ProcessRawDirectory(os9_path_id os9_path, const char* name)
{
	int ec;

	char dirname[30];
	int written = snprintf(dirname, 30, "dirs/%s", name);
	if (written < 4 || written >= 30) {
		fprintf(stderr, "Unable to make directory name\n");
		return 1;
	}
	ec = mkdir(dirname, 0777);
	if (ec) {
		fprintf(stderr, "Unable to create directory: %s\n", strerror(errno));
		return 1;
	}
	
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
		char name[32];

		os9_dir_entry *thisDEnt = ((os9_dir_entry*) dir_data) + k;

		if (thisDEnt->name[0] == '\0' && thisDEnt->name[1] != '\0')
		{
			thisDEnt->name[0] = '^';
		}

		OS9StringToCString(thisDEnt->name);
		strncpy(name, (char *)thisDEnt->name, 31);

		if (strcmp(name, ".") == 0)
		{
			continue;
		}

		if (strcmp(name, "..") == 0)
		{
			continue;
		}

		while (1) {
			char full_name[96];
			snprintf(full_name, 95, "%s/%s", dirname, name);
			// A file with the same name may exist already.
			// This can happen with single-character deleted files.
			if (access(full_name, F_OK) != 0) {
				break;
			}
			char new_name[32];
			snprintf(new_name, 31, "^%.29s", name);
			fprintf(stderr, "'%s/%s' is already taken, retrying with '%s/%s'.\n", dirname, name, dirname, new_name);
			strncpy(name, new_name, 31);
		}

		ec = ProcessDirectoryEntry(os9_path, dirname, name, int3(thisDEnt->lsn));
		if (ec) {
			fprintf(stderr, "error while processing directory entry #%d (%s), exiting.\n", k, name);
			break;
		}
	}

	free(dir_data);

	return ec;
}

static error_code MakeFileLink(const char* dirname, const char* name, const char* lsn_file_name)
{
	char link_name[64];
	snprintf(link_name, 63, "%s/%s", dirname, name);
	char target[64];
	snprintf(target, 63, "../../%s", lsn_file_name);

	int error_code = symlink(target, link_name);
	if (error_code) {
		fprintf(stderr, "Unable to symlink %s -> %s: %s\n", link_name, target, strerror(errno));
	}
	return error_code;
}

static error_code MakeDirLink(const char* dirname, const char* name, const char* target_dir)
{
	char link_name[64];
	snprintf(link_name, 63, "%s/%s", dirname, name);
	char target[64];
	snprintf(target, 63, "../%s", target_dir);

	int error_code = symlink(target, link_name);
	if (error_code) {
		fprintf(stderr, "Unable to symlink %s -> %s: %s\n", link_name, target, strerror(errno));
	}
	return error_code;
}

static error_code ProcessDirectoryEntry(os9_path_id os9_path, const char* dirname, const char* name, const u_int lsn)
{
	/*
	 * Given lsn 0xAABBCC:
	 * * There is "lsn_AABBCC.file" i.e. $AABBCC points to a file FD. Then this function creates a symlink in dirname
	 *   (assuming equal to dirs/lsn_DDEEFF.dir) that points to ../../lsn_AABBCC.file.
	 * * There is "lsn_AABBCC.dir" i.e. $AABBCC points to a directory FD. This this function creates a symlink in
	 *   dirname (assuming equal to dirs/lsn_DDEEFF.dir) that points to lsn_AABBCC.dir.
	 * * There is neither file. Show a message and return.
	*/
	char lsn_file_name[32];
	snprintf(lsn_file_name, 31, "lsn_%06x.dir", lsn);
	if (access(lsn_file_name, F_OK) == 0) {
		return MakeDirLink(dirname, name, lsn_file_name);
	}

	snprintf(lsn_file_name, 31, "lsn_%06x.file", lsn);
	if (access(lsn_file_name, F_OK) == 0) {
		return MakeFileLink(dirname, name, lsn_file_name);
	}

	fprintf(stderr, "Neither 'lsn_%06x.dir' or 'lsn_%06x.file' were found for entry '%s/%s'. Assuming it's a file and hope for the best.\n", lsn, lsn, dirname, name);
	snprintf(lsn_file_name, 31, "lsn_%06x.file", lsn);
	return MakeFileLink(dirname, name, lsn_file_name);
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
