/*
 * ftreecmp
 *
 * fast-ish comparison of a hierarchy of files
 *
 * Copyright (C) 2025 SUSE Linux
 * Written by okir@suse.com
 */

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>

#include "fstate.h"

static bool			opt_debug = false;

static bool			compare_directories(struct report *report, struct dstate *old, struct dstate *new);
static bool			compare_files(struct report *report, struct fstate *old, struct fstate *new);
static bool			report_recursively(struct report *report, int how, struct fstate *fs);

static void
usage(int exitval)
{
	fprintf(stderr,
		"Usage: ftreecmp [-dh] old_dir new_dir\n"
		" -d    enable debugging output\n"
		" -h    display this help message output\n"
	       );
	exit(exitval);
}

int
main(int argc, char **argv)
{
	char *opt_package_name = NULL;
	struct report *report;
	struct dstate *old, *new;
	int exitval = 0;
	int c;

	while ((c = getopt(argc, argv, "dhN:")) != -1) {
		switch (c) {
		case 'd':
			opt_debug = true;
			break;

		case 'N':
			opt_package_name = optarg;
			break;

		case 'h':
			usage(0);
		default:
			usage(1);
		}
	}

	if (argc - optind != 2)
		usage(1);

	report = report_new(opt_package_name);

	old = dstate_new(argv[optind++]);
	new = dstate_new(argv[optind++]);

	if (!dstate_read(old) || !dstate_read(new)
	 || !compare_directories(report, old, new))
		exitval = 1;

	dstate_free(old);
	dstate_free(new);
	report_free(report);

	return exitval;
}

/*
 * Recursively compare two directories
 */
static bool
compare_directories(struct report *report, struct dstate *old, struct dstate *new)
{
	struct fstate *old_fs = NULL, *new_fs = NULL;
	bool status = true;

	if (opt_debug)
		printf("D: Comparing %s vs %s\n", old->path, new->path);

	old->cursor = 0;
	new->cursor = 0;

	while (true) {
		int rv;

		if ((old_fs = dstate_current_entry(old)) == NULL) {
			while ((new_fs = dstate_current_entry(new)) != NULL) {
				report_recursively(report, FSTATE_CHANGED_ADDED, new_fs);
				new->cursor += 1;
			}
			break;
		}

		if ((new_fs = dstate_current_entry(new)) == NULL) {
			while ((old_fs = dstate_current_entry(old)) != NULL) {
				report_recursively(report, FSTATE_CHANGED_REMOVED, old_fs);
				old->cursor += 1;
			}
			break;
		}

		rv = strcmp(old_fs->name, new_fs->name);
		if (rv < 0) {
			report_recursively(report, FSTATE_CHANGED_REMOVED, old_fs);
			old->cursor += 1;
		} else if (rv > 0) {
			report_recursively(report, FSTATE_CHANGED_ADDED, new_fs);
			new->cursor += 1;
		} else {
			if (!compare_files(report, old_fs, new_fs))
				status = false;
			new->cursor += 1;
			old->cursor += 1;
		}
	}

	return status;
}


/*
 * Compare the contents of two regular files
 */
static bool
compare_regular_files(struct report *report, struct fstate *old, struct fstate *new)
{
	struct stat *old_stat = old->stb;
	struct stat *new_stat = new->stb;
	int old_fd, new_fd;
	int status = true;

	if (old_stat->st_size != new_stat->st_size)
		return false;

	if ((old_fd = fstate_open(old)) < 0)
		return false;
	if ((new_fd = fstate_open(new)) < 0) {
		close(old_fd);
		return false;
	}

	if (opt_debug)
		printf("D: comparing regular files %s vs %s\n", old->name, new->name);

	while (status) {
		unsigned char old_buf[8192], new_buf[8192];
		int old_len, new_len;

		if ((old_len = read(old_fd, old_buf, sizeof(old_buf))) < 0) {
			fprintf(stderr, "Error: failed to read from %s: %m\n", fstate_path(old));
			status = false;
			break;
		}

		if ((new_len = read(new_fd, new_buf, sizeof(new_buf))) < 0) {
			fprintf(stderr, "Error: failed to read from %s: %m\n", fstate_path(new));
			status = false;
			break;
		}

		if (old_len != new_len || memcmp(old_buf, new_buf, old_len)) {
			status = false;
			break;
		}

		if (old_len == 0)
			break;
	}

	close(old_fd);
	close(new_fd);

	return status;
}

/*
 * compare two directory entries an reports any discrepancies to stdout.
 * Returns false iff there was an error
 */
static bool
compare_files(struct report *report, struct fstate *old, struct fstate *new)
{
	bool status = true;

	if (old->type != new->type) {
		report_changed_file(report, FSTATE_CHANGED_REMOVED, old);
		report_changed_file(report, FSTATE_CHANGED_ADDED, new);
	} else {
		struct stat *old_stb, *new_stb;
		int how = 0;

		if (!(old_stb = fstate_stat(old)) || !(new_stb = fstate_stat(new)))
			return false;

		if ((S_ISUID|S_ISGID|S_ISVTX) & (old_stb->st_mode ^ new_stb->st_mode))
			how |= FSTATE_CHANGED_CRIT;
		if (old_stb->st_uid != new_stb->st_uid
		 || old_stb->st_gid != new_stb->st_gid)
			how |= FSTATE_CHANGED_CRIT;
		if (ALLPERMS & (old_stb->st_mode ^ new_stb->st_mode))
			how |= FSTATE_CHANGED_MODE;

		switch (old->type) {
		case DT_REG:
			if (!compare_regular_files(report, old, new))
				how |= FSTATE_CHANGED_DATA;
			break;

		case DT_LNK:
			{
				const char *old_link, *new_link;

				if (!(old_link = fstate_readlink(old))
				 || !(new_link = fstate_readlink(new))) {
					status = false;
				} else if (strcmp(old_link, new_link))
					how |= FSTATE_CHANGED_DATA;
			}
			break;

		case DT_CHR:
		case DT_BLK:
			if (old_stb->st_rdev != new_stb->st_rdev)
				how |= FSTATE_CHANGED_DATA;
			break;

		default:
			/* no checks beyond basic inode attr checks */
		}

		if (how != 0) {
			report_changed_file(report, how | FSTATE_CHANGED_REMOVED, old);
			report_changed_file(report, how | FSTATE_CHANGED_ADDED, new);
		}


		if (old->type == DT_DIR) {
			struct dstate *old_subdir, *new_subdir;

			old_subdir = fstate_descend(old);
			new_subdir = fstate_descend(new);
			status = compare_directories(report, old_subdir, new_subdir);
			dstate_free(old_subdir);
			dstate_free(new_subdir);
		}
	}
	return status;
}

static bool
report_recursively(struct report *report, int how, struct fstate *fs)
{
	const char *path = fstate_path(fs);
	struct stat *stb;
	bool status = true;

	if (!(stb = fstate_stat(fs))) {
		fprintf(stderr, "Error: failed to stat %s: %m\n", path);
		return false;
	}

	if (!report_changed_file(report, how, fs))
		return false;

	if (fs->type == DT_DIR) {
		struct dstate *subdir;
		struct fstate *entry;

		subdir = fstate_descend(fs);
		while ((entry = dstate_current_entry(subdir)) != NULL) {
			if (!report_recursively(report, how, entry))
				status = false;
			subdir->cursor += 1;
		}

		dstate_free(subdir);
	}
	return status;
}
