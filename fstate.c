/*
 * ftreecmp
 * 
 * helper functions for dealing with files and directories
 *
 * Copyright (C) 2025 SUSE Linux
 * Written by okir@suse.com
 */

#include <sys/sysmacros.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>

#include "fstate.h"

static inline void
__drop_string(char **vp)
{
	char *s;

	if ((s = *vp) != NULL)
		free(s);
	*vp = NULL;
}

struct fstate *
fstate_new(const char *name, int type)
{
	struct fstate *fs;

	fs = calloc(1, sizeof(*fs) + 1);
	fs->name = strdup(name);
	fs->type = type;

	return fs;
}

void
fstate_free(struct fstate *fs)
{
	__drop_string(&fs->name);
	__drop_string(&fs->path);
	__drop_string(&fs->link_dest);

	if (fs->stb)
		free(fs->stb);

	memset(fs, 0, sizeof(*fs));
	free(fs);
}

const char *
fstate_path(struct fstate *fs)
{
	if (fs->path == NULL) {
		char pathbuf[PATH_MAX];

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", fs->parent->path, fs->name);
		fs->path = strdup(pathbuf);
	}
	return fs->path;
}

static int
fstate_compare_name(const void *a, const void *b)
{
	return strcmp((*(const struct fstate **) a)->name,
			(*(const struct fstate **) b)->name);
}

struct dstate *
fstate_descend(struct fstate *fs)
{
	struct dstate *ds;

	if ((ds = dstate_new(fstate_path(fs))) != NULL) {
		if (!dstate_read(ds)) {
			dstate_free(ds);
			return NULL;
		}
	}

	return ds;
}

int
fstate_open(struct fstate *fs)
{
	const char *path = fstate_path(fs);
	int fd;

	if ((fd = open(path, O_RDONLY)) < 0) {
		fprintf(stderr, "Error: unable to open %s: %m\n", path);
		return -1;
	}
	return fd;
}

struct stat *
fstate_stat(struct fstate *fs)
{
	if (fs->stb == NULL) {
		const char *path = fstate_path(fs);
		struct stat stb;

		if (lstat(path, &stb) < 0) {
			fprintf(stderr, "Error: unable to stat %s: %m\n", path);
			return NULL;
		}

		fs->stb = malloc(sizeof(stb));
		memcpy(fs->stb, &stb, sizeof(stb));
	}
	return fs->stb;
}

bool
fstate_isdir(struct fstate *fs)
{
	return fs->type == DT_DIR;
}

const char *
fstate_readlink(struct fstate *fs)
{
	if (fs->link_dest == NULL) {
		const char *path = fstate_path(fs);
		char pathbuf[PATH_MAX];

		if (readlink(path, pathbuf, sizeof(pathbuf)) < 0) {
			fprintf(stderr, "Error: readlink(%s) failed: %m\n", path);
			return NULL;
		}
		fs->link_dest = strdup(pathbuf);
	}

	return fs->link_dest;
}

struct dstate *
dstate_new(const char *path)
{
	struct dstate *ds;

	ds = calloc(1, sizeof(*ds) + 1);
	ds->path = strdup(path);
	return ds;
}

void
dstate_free(struct dstate *ds)
{
	unsigned int i;

	for (i = 0; i < ds->count; ++i)
		fstate_free(ds->files[i]);

	memset(ds, 0, sizeof(*ds));
	free(ds);
}

static struct fstate *
dstate_add_entry(struct dstate *ds, const char *name, int type)
{
	struct fstate *fs;

	fs = fstate_new(name, type);
	if ((ds->count % 16) == 0)
		ds->files = reallocarray(ds->files, ds->count + 16, sizeof(ds->files[0]));
	ds->files[ds->count++] = fs;
	fs->parent = ds;

	return fs;
}

bool
dstate_read(struct dstate *ds)
{
	DIR *dir;
	struct dirent *de;

	if (!(dir = opendir(ds->path))) {
		fprintf(stderr, "Error: unable to open directory %s: %m\n", ds->path);
		return false;
	}

	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, ".")
		 || !strcmp(de->d_name, ".."))
			continue;

		dstate_add_entry(ds, de->d_name, de->d_type);
	}
	closedir(dir);

	qsort(ds->files, ds->count, sizeof(ds->files[0]), fstate_compare_name);

	return true;
}

struct fstate *
dstate_current_entry(struct dstate *ds)
{
	if (ds->cursor >= ds->count)
		return NULL;
	return ds->files[ds->cursor];
}

