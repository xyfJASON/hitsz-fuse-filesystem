#ifndef _XYFFS_H_
#define _XYFFS_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <assert.h>
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"

#define XYFFS_MAGIC           998244353 /* TODO: Define by yourself */
#define XYFFS_DEFAULT_PERM    0777   /* 全权限打开 */

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

/******************************************************************************
* SECTION: xyffs.c
*******************************************************************************/
void* 			   xyffs_init(struct fuse_conn_info *);
void  			   xyffs_destroy(void *);
int   			   xyffs_mkdir(const char *, mode_t);
int   			   xyffs_getattr(const char *, struct stat *);
int   			   xyffs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);
int   			   xyffs_mknod(const char *, mode_t, dev_t);
int   			   xyffs_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);
int   			   xyffs_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);
int   			   xyffs_access(const char *, int);
int   			   xyffs_unlink(const char *);
int   			   xyffs_rmdir(const char *);
int   			   xyffs_rename(const char *, const char *);
int   			   xyffs_utimens(const char *, const struct timespec tv[2]);
int   			   xyffs_truncate(const char *, off_t);
			
int   			   xyffs_open(const char *, struct fuse_file_info *);
int   			   xyffs_opendir(const char *, struct fuse_file_info *);

#endif  /* _xyffs_H_ */
