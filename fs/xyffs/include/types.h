#ifndef _TYPES_H_
#define _TYPES_H_

#define MAX_NAME_LEN    128     

struct custom_options {
	const char*        device;
};

#define T_FILE 1
#define T_DIR  2

#define SZ_DISK (4*1024*1024)
#define MAX_INODE (8*1024)

struct xyffs_super_d {
    uint32_t magic;

    int      inode_bitmap_blks;
    int      inode_bitmap_offset;
    int      data_bitmap_blks;
    int      data_bitmap_offset;
};
struct xyffs_super {
    int      fd;

    char*    inode_bitmap;
    char*    data_bitmap;
    struct xyffs_dentry* root_dentry;

    int      inode_bitmap_blks;
    int      inode_bitmap_offset;
    int      data_bitmap_blks;
    int      data_bitmap_offset;
    int      inode_blks;
    int      inode_offset;
    int      data_blks;
    int      data_offset;
};

struct xyffs_inode_d {
    uint32_t  ino;
    int       size;
    int       type;
    int       dircnt;
    int       datablock[6];
};
struct xyffs_inode {
    uint32_t  ino;
    int       size;
    int       type;
    int       dircnt;
    struct xyffs_dentry *dentry_pt;
    struct xyffs_dentry *dentry_sons;
    int       datablock[6];
    char*     datablock_pt[6];
};

struct xyffs_dentry_d {
    char      name[MAX_NAME_LEN];
    int       type;
    int       ino;
};
struct xyffs_dentry {
    char      name[MAX_NAME_LEN];
    int       type;
    int       ino;
    struct xyffs_inode *inode_pt;
    struct xyffs_dentry *nxt;
    struct xyffs_dentry *fa;
};

#endif /* _TYPES_H_ */
