/** @file
 *
 * Compile with:
 *
 *     gcc -Wall onflyfs.c `pkg-config fuse --cflags --libs` -lulockmgr -o rotatefs
 *
 */

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 30 

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <fuse.h>

#ifdef HAVE_LIBULOCKMGR
#include <ulockmgr.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <sys/file.h> /* flock(2) */

#include <ftw.h>
#include <limits.h>
#include <sys/types.h>

static char *rootdir;

//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static void fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
				    // break here
}

static void *onflyfs_init(struct fuse_conn_info *conn)
{
	(void) conn;

	return NULL;
}

static int onflyfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = lstat(fpath, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = fstat(fi->fh, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_access(const char *path, int mask)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = access(fpath, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_readlink(const char *path, char *buf, size_t size)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = readlink(fpath, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct onflyfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int onflyfs_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	struct onflyfs_dirp *d = malloc(sizeof(struct onflyfs_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(fpath);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

static inline struct onflyfs_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct onflyfs_dirp *) (uintptr_t) fi->fh;
}

static int onflyfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct onflyfs_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int onflyfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct onflyfs_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

static int onflyfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	if (S_ISFIFO(mode))
		res = mkfifo(fpath, mode);
	else
		res = mknod(fpath, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_mkdir(const char *path, mode_t mode)
{
	printf("--> Creating directory %s\n", path);
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = mkdir(fpath, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_unlink(const char *path)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = unlink(fpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_rmdir(const char *path)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = rmdir(fpath);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_symlink(const char *from, const char *to)
{
	int res;
        char ffrom[PATH_MAX];
        char fto[PATH_MAX];

        fullpath(ffrom, from);
        fullpath(fto, to);
	res = symlink(ffrom, fto);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_rename(const char *from, const char *to)
{
	int res;
        char ffrom[PATH_MAX];
        char fto[PATH_MAX];

        fullpath(ffrom, from);
        fullpath(fto, to);
	res = rename(ffrom, fto);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_link(const char *from, const char *to)
{
	int res;
        char ffrom[PATH_MAX];
        char fto[PATH_MAX];

        fullpath(ffrom, from);
        fullpath(fto, to);
	res = link(ffrom, fto);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_chmod(const char *path, mode_t mode)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
        res = chmod(fpath, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
        res = lchown(fpath, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_truncate(const char *path, off_t size)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = truncate(fpath, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int onflyfs_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	int res;
        char fpath[PATH_MAX];

	/* don't use utime/utimes since they follow symlinks */
	if (fi)
            res = futimens(fi->fh, ts);
        else {
            fullpath(fpath, path);
            res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
        }
	if (res == -1)
            return -errno;

	return 0;
}
#endif

static int onflyfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	fd = open(fpath, fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int onflyfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	fd = open(fpath, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int onflyfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int onflyfs_read_buf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src;

	(void) path;

	src = malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = offset;

	*bufp = src;

	return 0;
}

static int onflyfs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res = 0;

	(void) path;

	if (res == -1)
		res = -errno;

	return res;
}

static int onflyfs_write_buf(const char *path, struct fuse_bufvec *buf,
		     off_t offset, struct fuse_file_info *fi)
{
        int res = 0;
	return res;
}

static int onflyfs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	res = statvfs(fpath, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int onflyfs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);

	return 0;
}

static int onflyfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;
	(void) path;

#ifndef HAVE_FDATASYNC
	(void) isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
		res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int onflyfs_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void) path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int onflyfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = lsetxattr(fpath, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int onflyfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = lgetxattr(fpath, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int onflyfs_listxattr(const char *path, char *list, size_t size)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = llistxattr(fpath, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int onflyfs_removexattr(const char *path, const char *name)
{
        char fpath[PATH_MAX];

        fullpath(fpath, path);
	int res = lremovexattr(fpath, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

#ifdef HAVE_LIBULOCKMGR
static int onflyfs_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}
#endif

static int onflyfs_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int res;
	(void) path;

	res = flock(fi->fh, op);
	if (res == -1)
		return -errno;

	return 0;
}

static struct fuse_operations onflyfs_oper = {
	//.init           = onflyfs_init,
	.getattr	= onflyfs_getattr,
	.fgetattr	= onflyfs_fgetattr,
	.access		= onflyfs_access,
	.readlink	= onflyfs_readlink,
	.opendir	= onflyfs_opendir,
	.readdir	= onflyfs_readdir,
	.releasedir	= onflyfs_releasedir,
	.mknod		= onflyfs_mknod,
	.mkdir		= onflyfs_mkdir,
	.symlink	= onflyfs_symlink,
	.unlink		= onflyfs_unlink,
	.rmdir		= onflyfs_rmdir,
	.rename		= onflyfs_rename,
	.link		= onflyfs_link,
	.chmod		= onflyfs_chmod,
	.chown		= onflyfs_chown,
	.truncate	= onflyfs_truncate,
	.ftruncate	= onflyfs_ftruncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= onflyfs_utimens,
#endif
	.create		= onflyfs_create,
	.open		= onflyfs_open,
	.read		= onflyfs_read,
	.read_buf	= onflyfs_read_buf,
	.write		= onflyfs_write,
	.write_buf	= onflyfs_write_buf,
	.statfs		= onflyfs_statfs,
	.flush		= onflyfs_flush,
	.release	= onflyfs_release,
	.fsync		= onflyfs_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= onflyfs_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= onflyfs_setxattr,
	.getxattr	= onflyfs_getxattr,
	.listxattr	= onflyfs_listxattr,
	.removexattr	= onflyfs_removexattr,
#endif
#ifdef HAVE_LIBULOCKMGR
	.lock		= onflyfs_lock,
#endif
	.flock		= onflyfs_flock,
};

void onflyfs_usage()
{
    fprintf(stderr, "usage:  onflyfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    umask(0);

    // See which version of fuse we're running
    fprintf(stderr, "Fuse library version %d.%d\n", FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION);

    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments, and that neither of the last two
    // start with a hyphen (this will break if you actually have a
    // rootpoint or mountpoint whose name starts with a hyphen, but so
    // will a zillion other programs)
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
        onflyfs_usage();

    rootdir  = malloc(sizeof(char) * 1024);
    if (rootdir == NULL) {
	perror("Unable to allocate memory for rootdir name\n");
	abort();
    }
    rootdir = realpath(argv[argc-2], NULL);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    fprintf(stderr, "rootdir: %s\n", rootdir);

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, NULL, NULL, NULL) == -1)
        onflyfs_usage();

    // turn over control to fuse
    return fuse_main(args.argc, args.argv, &onflyfs_oper, rootdir);
    
}
