/*
 * ftreecmp
 * 
 * declaration of types and functions for dealing with files and directories
 *
 * Copyright (C) 2025 SUSE Linux
 * Written by okir@suse.com
 */

#ifndef FSTATE_H
#define FSTATE_H

#include <sys/stat.h>

/* Represents any sort of directory entry */
struct fstate {
	/* These are initialized from readdir info inside dstate_read() */
	struct dstate	*parent;
	char *		name;
	int		type;

	/* the remainder is populated on-demand */

	/* fully qualified path */
	char *		path;

	/* the stat buffer */
	struct stat *	stb;

	/* symlink destination */
	char *		link_dest;
};

/* Represents a directory that we want to descend into */
struct dstate {
	char *		path;
	DIR *		f;

	unsigned int	cursor;

	unsigned int	count;
	struct fstate **files;
};

extern struct dstate *		dstate_new(const char *path);
extern void			dstate_free(struct dstate *ds);
extern bool			dstate_read(struct dstate *ds);
extern struct fstate *		dstate_current_entry(struct dstate *ds);

extern const char *		fstate_path(struct fstate *fs);
extern struct dstate *		fstate_descend(struct fstate *fs);
extern int			fstate_open(struct fstate *fs);
extern struct stat *		fstate_stat(struct fstate *fs);
extern const char *		fstate_readlink(struct fstate *fs);

struct report;

extern struct report *		report_new(const char *package_name);
extern void			report_free(struct report *);

#define FSTATE_CHANGED_CRIT	0x0001	/* file type, owner, set*id bits, sticky bits ... */
#define FSTATE_CHANGED_MODE	0x0002	/* file modes */
#define FSTATE_CHANGED_DATA	0x0004	/* file content, incl link tgt */
#define FSTATE_CHANGED_ADDED	0x0010
#define FSTATE_CHANGED_REMOVED	0x0020

extern bool			report_changed_file(struct report *report, int how, struct fstate *fs);

#endif /* FSTATE_H */
