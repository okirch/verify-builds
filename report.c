/*
 * Reporting functions
 *
 * Copyright (C) 2025 SUSE Linux
 * Written by okir@suse.com
 */

#include <sys/sysmacros.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>

#include "fstate.h"

struct report {
	char *		package_name;
	unsigned int	lines_written;
};

static void		__render_change_bit_legend(struct report *report);

struct report *
report_new(const char *package_name)
{
	struct report *report;

	if (package_name == NULL)
		package_name = "<unknown package>";

	report = calloc(1, sizeof(*report));
	report->package_name = strdup(package_name);
	return report;
}

void
report_free(struct report *report)
{
	if (report->lines_written)
		__render_change_bit_legend(report);

	if (report->package_name)
		free(report->package_name);
	report->package_name = NULL;
	free(report);
}

static void
report_printf(struct report *report, const char *fmt, ...)
{
	va_list ap;

	if (!report->lines_written++)
		printf("%s: file changes\n", report->package_name);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static char
mode_to_filetype(unsigned long mode)
{
	switch (mode & S_IFMT) {
	case S_IFDIR:
		return 'd';
	case S_IFREG:
		return '-';
	case S_IFCHR:
		return 'c';
	case S_IFBLK:
		return 'b';
	case S_IFLNK:
		return 'l';
	case S_IFSOCK:
		return 's';
	case S_IFIFO:
		return 'f';
	}
	return '?';
}

static inline char
__bit_to_sym(unsigned long mode, unsigned long mask, char cc_ifset, char cc_ifnotset)
{
	if (mode & mask)
		return cc_ifset;
	return cc_ifnotset;
}

static inline char
mode_bit_to_sym(unsigned long mode, unsigned long mask, char cc)
{
	return __bit_to_sym(mode, mask, cc, '-');
}

static inline char
mode_bit_to_sym2(unsigned long mode, unsigned long mask1, char cc1, unsigned long mask2, char cc2)
{
	if (mode & mask1) {
		if (mode & mask2)
			return cc2;
		return cc1;
	} else if (mode & mask2) {
		return toupper(cc2);
	}
	return '-';
}

static inline char
change_bit_to_sym(unsigned long mode, unsigned long mask, char cc)
{
	return __bit_to_sym(mode, mask, cc, '.');
}

static const char *
symbolic_permissions(unsigned long mode)
{
	static char buffer[32];
	unsigned int i = 0;

	buffer[i++] = mode_to_filetype(mode);
	buffer[i++] = mode_bit_to_sym(mode, S_IRUSR, 'r');
	buffer[i++] = mode_bit_to_sym(mode, S_IWUSR, 'w');
	buffer[i++] = mode_bit_to_sym2(mode, S_IXUSR, 'x', S_ISUID, 's');
	buffer[i++] = mode_bit_to_sym(mode, S_IRGRP, 'r');
	buffer[i++] = mode_bit_to_sym(mode, S_IWGRP, 'w');
	buffer[i++] = mode_bit_to_sym2(mode, S_IXGRP, 'x', S_ISGID, 's');
	buffer[i++] = mode_bit_to_sym(mode, S_IROTH, 'r');
	buffer[i++] = mode_bit_to_sym(mode, S_IWOTH, 'w');
	buffer[i++] = mode_bit_to_sym2(mode, S_IXOTH, 'x', S_ISVTX, 't');

	buffer[i] = '\0';
	return buffer;
}

static char *
__render_attrs(const struct stat *stb)
{
	static char buffer[128];

	snprintf(buffer, sizeof(buffer),
			"%s uid %03u gid %03u",
			symbolic_permissions(stb->st_mode),
			stb->st_uid, stb->st_gid);
	return buffer;
}

static void
__report_inode(struct report *report, const char *pfx, struct fstate *fs)
{
	const struct stat *stb = fs->stb;

	report_printf(report, "%-12s %s               %s\n",
			pfx, __render_attrs(stb),
			fstate_path(fs));
}

static void
__report_regular_file(struct report *report, const char *pfx, struct fstate *fs)
{
	const struct stat *stb = fs->stb;

	report_printf(report, "%-12s %s %13lu %s\n",
			pfx, __render_attrs(stb),
			(unsigned long) stb->st_size,
			fstate_path(fs));
}

static void
__report_symlink(struct report *report, const char *pfx, struct fstate *fs)
{
	const char *dest = fs->link_dest;
	struct stat *stb = fs->stb;

	report_printf(report, "%-12s %s               %s -> %s\n",
			pfx, __render_attrs(stb),
			fstate_path(fs), dest);
}

static void
__report_device(struct report *report, const char *pfx, struct fstate *fs)
{
	struct stat *stb = fs->stb;

	report_printf(report, "%-12s %s dev %04x:%04x %s\n",
			pfx, __render_attrs(stb),
			major(stb->st_rdev), minor(stb->st_rdev),
			fstate_path(fs));
}

const char *
__render_change_bits(int how)
{
	static char buf[32];
	int i = 0;

	buf[i++] = ' ';
	buf[i++] = ' ';
	buf[i++] = ' ';

	if (how & FSTATE_CHANGED_ADDED)
		buf[i++] = '+';
	else if (how & FSTATE_CHANGED_REMOVED)
		buf[i++] = '-';
	else
		buf[i++] = '?';
	buf[i++] = ' ';

	buf[i++] = change_bit_to_sym(how, FSTATE_CHANGED_CRIT, 'C');
	buf[i++] = change_bit_to_sym(how, FSTATE_CHANGED_MODE, 'M');
	buf[i++] = change_bit_to_sym(how, FSTATE_CHANGED_DATA, 'D');
	buf[i++] = ' ';

	buf[i] = '\0';
	return buf;
}

static void
__render_change_bit_legend(struct report *report)
{
	report_printf(report, "\nDescription of change bits:\n");
	report_printf(report, " +   added\n");
	report_printf(report, " -   removed\n");
	report_printf(report, " C   critical change (file type, owner, set*id bits etc)\n");
	report_printf(report, " M   mode change (file permissions)\n");
	report_printf(report, " D   data change (file content, symlink target, device major/minor)\n");
	report_printf(report, "\n");
}

bool
report_changed_file(struct report *report, int how, struct fstate *fs)
{
	const struct stat *stb;
	const char *pfx;

	if (!(stb = fstate_stat(fs)))
		return false;

	pfx = __render_change_bits(how);

	switch (fs->type) {
	case DT_REG:
		__report_regular_file(report, pfx, fs);
		break;
	case DT_LNK:
		if (!fstate_readlink(fs))
			return false;
		__report_symlink(report, pfx, fs);
		break;
	case DT_CHR:
	case DT_BLK:
		__report_device(report, pfx, fs);
		break;
	default:
		__report_inode(report, pfx, fs);
		break;
	}

	return true;
}

