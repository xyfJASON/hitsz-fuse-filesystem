#include "xyffs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options xyffs_options;			 /* 全局选项 */
struct xyffs_super super; 

/******************************************************************************
* SECTION: 功能函数 (added by xyf) 
*******************************************************************************/
int read_disk(int offset, char *buf, int size){
  // 从 offset 开始，读 size 个字节，存入 buf
  // offset 和 offset+size 可能不与 512 对齐
  int offset_align = offset / 512 * 512;
  int offset_bias = offset - offset_align;
  char *tmp = (char *)malloc((offset_bias + size + 512 - 1) / 512 * 512);
  char *tmppt = tmp;
  ddriver_seek(super.fd, offset_align, SEEK_SET);
  for(int i = offset_align; i < offset+size; i += 512){
    ddriver_read(super.fd, tmppt, 512);
    tmppt += 512;
  }
  memcpy(buf, tmp + offset_bias, size);
  free(tmp);
  return 0;
}
int write_disk(int offset, char *buf, int size){
  // 从 offset 开始，写 size 个字节
  // offset 和 offset+size 可能不与 512 对齐
  int offset_align = offset / 512 * 512;
  int offset_bias = offset - offset_align;
  int size_align = (offset_bias + size + 512 - 1) / 512 * 512;
  char *tmp = (char *)malloc(size_align);
  read_disk(offset_align, tmp, size_align);
  memcpy(tmp + offset_bias, buf, size); 
  char *tmppt = tmp;
  ddriver_seek(super.fd, offset_align, SEEK_SET);
  for(int i = offset_align; i < offset+size; i += 512){
    ddriver_write(super.fd, tmppt, 512);
    tmppt += 512;
  }
  free(tmp);
  return 0;
}

void allocdatablock(char *bpt, int *bno){
  // 开辟一个内存块，返回指针和编号
  *bno = -1;
  for(int byte = 0; byte < super.data_bitmap_blks * 1024; byte++){
    for(int bit = 0; bit < 8; bit++){
      if(((super.data_bitmap[byte] >> bit) & 1) == 0){
        *bno = byte * 8 + bit;
        super.data_bitmap[byte] |= (1 << bit);
        break;
      }
    }
    if(*bno != -1)  break;
  }
  if(*bno == -1){
    printf("\033[1;31mNo free data blocks to allocate!\033[0m\n");
    return;
  }
  bpt = (char *)malloc(1024);
}

struct xyffs_dentry *newdentry(char *name, int type, int ino){
  struct xyffs_dentry *newdentry = (struct xyffs_dentry *)malloc(sizeof(struct xyffs_dentry));
  memcpy(newdentry->name, name, MAX_NAME_LEN);
  newdentry->type = type;
  newdentry->ino = ino;
  newdentry->inode_pt = NULL;
  newdentry->nxt = NULL;
  newdentry->fa = NULL;
  return newdentry;
}
struct xyffs_inode *allocinode(struct xyffs_dentry *dentry, int type){
  // 创建一个新的inode，并与dentry相关联
  int freeno = -1;
  for(int byte = 0; byte < super.inode_bitmap_blks * 1024; byte++){
    for(int bit = 0; bit < 8; bit++){
      if(((super.inode_bitmap[byte] >> bit) & 1) == 0){
        freeno = byte * 8 + bit;
        super.inode_bitmap[byte] |=  (1 << bit);
        break;
      }
    }
    if(freeno != -1)  break;
  }
  if(freeno == -1){
    printf("\033[1;31mNo free inodes to allocate!\033[0m\n");
    return 0;
  }
  struct xyffs_inode *newinode = (struct xyffs_inode *)malloc(sizeof(struct xyffs_inode));
  newinode->ino = freeno;
  newinode->size = 0;
  newinode->type = type;
  newinode->dircnt = 0;
  newinode->dentry_pt = dentry;
  newinode->dentry_sons = NULL;
  for(int k = 0; k < 6; k++){
    newinode->datablock[k] = -1;
    newinode->datablock_pt[k] = NULL;
  }
  dentry->inode_pt = newinode;
  dentry->ino = newinode->ino;
  assert(dentry->type == newinode->type);
  return newinode;
}
struct xyffs_inode *get_inode_from_dentry(struct xyffs_dentry *dentry){
  // 根据dentry读入inode
  // 若inode是目录，则创建其目录项的subdentry内存结构并形成链表，同时父指针链接到dentry
  struct xyffs_inode *inode = (struct xyffs_inode *)malloc(sizeof(struct xyffs_inode));
  struct xyffs_inode_d inode_d;
  read_disk((super.inode_offset + dentry->ino) * 1024, (char *)&inode_d, sizeof(struct xyffs_inode_d));
  inode->ino = inode_d.ino;
  inode->size = inode_d.size;
  inode->type = inode_d.type;
  inode->dircnt = inode_d.dircnt;
  inode->dentry_pt = dentry;
  inode->dentry_sons = NULL;
  dentry->inode_pt = inode;
  assert(dentry->ino == inode->ino);
  assert(dentry->type == inode->type);
  for(int k = 0; k < 6; k++){
    inode->datablock[k] = inode_d.datablock[k];
    if(inode_d.datablock[k] != -1){
      char *content = (char *)malloc(1024);
      read_disk((super.data_offset + inode->datablock[k]) * 1024, content, 1024);
      inode->datablock_pt[k] = content;
    }
  }

  if(inode->type == T_DIR){
    // 根据datablock内容读入目录项
    struct xyffs_dentry_d subdentry_d;
    int dirs_per_blk = 1024 / sizeof(struct xyffs_dentry_d), last_blkno = -1;
    for(int i = 0, offset = 0; i < inode->dircnt; i++, offset += sizeof(struct xyffs_dentry_d)){
      // 读入第i个目录项到subdentry_d
      if(i / dirs_per_blk != last_blkno){
        offset = (super.data_offset + inode->datablock[i/dirs_per_blk]) * 1024;
        last_blkno = i / dirs_per_blk;
      }
      read_disk(offset, (char *)&subdentry_d, sizeof(struct xyffs_dentry_d));
      // 建立对应内存目录项，插入链表，设置父指针
      struct xyffs_dentry *subdentry = newdentry(subdentry_d.name, subdentry_d.type, subdentry_d.ino);
      subdentry->nxt = inode->dentry_sons;
      inode->dentry_sons = subdentry;
      subdentry->fa = dentry;
    }
  }

  return inode;
}

void write_back_recurse(struct xyffs_dentry *dentry){
  assert(dentry->type == T_DIR || dentry->type == T_FILE);
  struct xyffs_inode *inode = dentry->inode_pt;

  // 写inode
  struct xyffs_inode_d inode_d;
  inode_d.ino = inode->ino;
  inode_d.size = inode->size;
  inode_d.type = inode->type;
  inode_d.dircnt = inode->dircnt;
  for(int k = 0; k < 6; k++)
    inode_d.datablock[k] = inode->datablock[k];
  write_disk((super.inode_offset + inode->ino) * 1024, (char *)&inode_d, sizeof(struct xyffs_inode_d));

  // 写data
  if(dentry->type == T_DIR){
    struct xyffs_dentry_d dentry_d;
    struct xyffs_dentry *pt = inode->dentry_sons;
    int dirs_per_blk = 1024 / sizeof(struct xyffs_dentry_d), last_blkno = -1;
    int cnt = 0;
    for(int offset = 0; pt; pt = pt->nxt, offset += sizeof(struct xyffs_dentry_d), cnt++){
      if(cnt / dirs_per_blk != last_blkno){
        offset = (super.data_offset + inode->datablock[cnt/dirs_per_blk]) * 1024;
        last_blkno = cnt / dirs_per_blk;
      }
      memcpy(dentry_d.name, pt->name, MAX_NAME_LEN);
      dentry_d.type = pt->type;
      dentry_d.ino = pt->ino;
      write_disk(offset, (char *)&dentry_d, sizeof(struct xyffs_dentry_d));
      
      write_back_recurse(pt);
    }
    assert(cnt == inode->dircnt);
  }
  else{
    for(int k = 0; k < 6; k++)
      if(inode->datablock[k] != -1)
        write_disk((super.data_offset + inode->datablock[k]) * 1024, inode->datablock_pt[k], 1024);
  }
}

int calc_path_level(const char *path){
  char *str = (char *)path;
  int lvl = 0;
  if(strcmp(path, "/") == 0)  return lvl;
  while(*str != '\0'){
    if(*str == '/') lvl++;
    str++;
  }
  return lvl;
}

char *path2filename(const char *path){
  char ch = '/';
  char *q = strrchr(path, ch) + 1;
  return q;
}

struct xyffs_dentry *parsepath(const char *path, int *is_root, int *is_find){
  assert(path[0] == '/');
  struct xyffs_dentry *cur_dentry = super.root_dentry;
  struct xyffs_dentry *ret = NULL;

  char *pathcpy = (char *)malloc(sizeof(path));
  strcpy(pathcpy, path);
  *is_root = 0;
  int totlvl = calc_path_level(path);
  int lvl = 0;

  if(totlvl == 0){
    *is_root = 1;
    *is_find = 1;
    ret = super.root_dentry;
  }
  char *fname = strtok(pathcpy, "/");
  while(fname){
    lvl++;
    if(cur_dentry->inode_pt == NULL)
      get_inode_from_dentry(cur_dentry);
    struct xyffs_inode *inode = cur_dentry->inode_pt;
    if(inode->type == T_FILE && lvl < totlvl){
      ret = inode->dentry_pt;
      break;
    }
    if(inode->type == T_DIR){
      cur_dentry = inode->dentry_sons;
      int ok = 0;
      while(cur_dentry){
        if(memcmp(cur_dentry->name, fname, strlen(fname)) == 0){
          ok = 1; break;
        }
        cur_dentry = cur_dentry->nxt;
      }
      if(!ok){
        *is_find = 0;
        ret = inode->dentry_pt;
        break;
      }
      if(ok && lvl == totlvl){
        *is_find = 1;
        ret = cur_dentry;
        break;
      }
    }
    fname = strtok(NULL, "/");
  }
  if(ret->inode_pt == NULL)
    get_inode_from_dentry(ret);
  return ret;
}

struct xyffs_dentry *get_kth_subdentry(struct xyffs_inode *inode, int k){
  struct xyffs_dentry *cur = inode->dentry_sons;
  while(k--){
    if(!cur)  return NULL;
    cur = cur->nxt;
  }
  return cur;
}

/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = xyffs_init,						 /* mount文件系统 */		
	.destroy = xyffs_destroy,				 /* umount文件系统 */
	.mkdir = xyffs_mkdir,					 /* 建目录，mkdir */
	.getattr = xyffs_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = xyffs_readdir,				 /* 填充dentrys */
	.mknod = xyffs_mknod,					 /* 创建文件，touch相关 */
	.write = NULL,								  	 /* 写入文件 */
    .read = NULL,								  	 /* 读文件 */
	.utimens = xyffs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						  		 /* 改变文件大小 */
	.unlink = NULL,							  		 /* 删除文件 */
	.rmdir	= NULL,							  		 /* 删除目录， rm -r */
	.rename = NULL,							  		 /* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* xyffs_init(struct fuse_conn_info * conn_info) {
  int isinit = 0;

	super.fd = ddriver_open((char *)xyffs_options.device);
  assert(super.fd >= 0);

  /******************** 读入超级块 ********************/
  struct xyffs_super_d super_d;
  read_disk(0, (char *)&super_d, sizeof(struct xyffs_super_d));
  if(super_d.magic != XYFFS_MAGIC){
    // 幻数不匹配，初始化磁盘超级块数据结构
    // 设最多 8K 个文件 (MAX_INODE)
    // super(1) | inode bitmap(1) | data bitmap(1) | inode ceil(8K*sizeof(inode)/1K) | data
    isinit = 1;
    super_d.inode_bitmap_blks = 1;
    super_d.inode_bitmap_offset = 1;
    super_d.data_bitmap_blks = 1;
    super_d.data_bitmap_offset = super_d.inode_bitmap_offset + super_d.inode_bitmap_blks;
    printf("\033[1;32mNew device detected, initialized into xyffs.\033[0m\n");
  }
  /****************************************************/
  /*********** 将磁盘超级块载入内存超级块 *************/
  super.inode_bitmap_blks = super_d.inode_bitmap_blks;
  super.inode_bitmap_offset = super_d.inode_bitmap_offset;
  super.data_bitmap_blks = super_d.data_bitmap_blks;
  super.data_bitmap_offset = super_d.data_bitmap_offset;
  super.inode_blks = (MAX_INODE * sizeof(struct xyffs_inode_d) + 1024 - 1) / 1024;
  super.inode_offset = super.data_bitmap_offset + super.data_bitmap_blks;
  super.data_blks = (SZ_DISK / 1024) - 1 - super_d.inode_bitmap_blks - super_d.data_bitmap_blks - super.inode_blks;
  super.data_offset = super.inode_offset + super.inode_blks;

  super.inode_bitmap = (char *)malloc(super_d.inode_bitmap_blks * 1024);
  read_disk(super_d.inode_bitmap_offset * 1024, super.inode_bitmap, super_d.inode_bitmap_blks * 1024);
  super.data_bitmap = (char *)malloc(super_d.data_bitmap_blks * 1024);
  read_disk(super_d.data_bitmap_offset * 1024, super.data_bitmap, super_d.data_bitmap_blks * 1024);

  super.root_dentry = newdentry("/", T_DIR, 0);
  if(isinit){
    // 如果是初始化的文件系统，则初始化根inode
    struct xyffs_inode *root_inode = allocinode(super.root_dentry, T_DIR);
    assert(root_inode->ino == 0); // 根inode必定是0号
  }
  else{
    // 否则读入0号inode并与root_dentry关联
    get_inode_from_dentry(super.root_dentry);
  }
  /****************************************************/
  printf("\033[1;32mFile system mounted successfully.\033[0m\n");

	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void xyffs_destroy(void* p) {
	
  /********** 递归写更改过的inode块和数据块 ***********/
  write_back_recurse(super.root_dentry);
  /****************************************************/

  /********************* 写超级块 *********************/
  struct xyffs_super_d super_d;
  super_d.magic = XYFFS_MAGIC;
  super_d.inode_bitmap_blks = super.inode_bitmap_blks;
  super_d.inode_bitmap_offset = super.inode_bitmap_offset;
  super_d.data_bitmap_blks = super.data_bitmap_blks;
  super_d.data_bitmap_offset = super.data_bitmap_offset;
  write_disk(0, (char *)&super_d, sizeof(struct xyffs_super_d));
  /****************************************************/

  /********************* 写位图块 *********************/
  write_disk(super.inode_bitmap_offset * 1024, super.inode_bitmap, super.inode_bitmap_blks * 1024);
  write_disk(super.data_bitmap_offset * 1024, super.data_bitmap, super.data_bitmap_blks * 1024);
  /****************************************************/

  free(super.inode_bitmap);
  free(super.data_bitmap);
	ddriver_close(super.fd);

	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int xyffs_mkdir(const char* path, mode_t mode) {
  (void)mode;
  int is_root, is_find;
  struct xyffs_dentry *lastdentry = parsepath(path, &is_root, &is_find);
  struct xyffs_inode *lastinode = lastdentry->inode_pt;
  struct xyffs_dentry *dentry;

  if(is_find){
    printf("\033[1;31mDirectory name already exists!\033[0m\n");
    return -EEXIST;
  }
  if(lastinode->type == T_FILE)
    return -ENXIO;
  
  char *fname = path2filename(path);
  dentry = newdentry(fname, T_DIR, -1);
  allocinode(dentry, T_DIR);
  // lastinode加一个目录项
  int dirs_per_blk = 1024 / sizeof(struct xyffs_dentry_d);
  lastinode->dircnt++;
  if(lastinode->dircnt == 1 || ((lastinode->dircnt - 1) / dirs_per_blk != (lastinode->dircnt - 2) / dirs_per_blk)){
    // 需要新的数据块来存增加的目录项
    int blkno = (lastinode->dircnt - 1) / dirs_per_blk;
    if(blkno >= 6){
      printf("\033[1;31mNo enough memory!\033[0m\n");
      return -1;
    }
    char *bpt; int bno;
    allocdatablock(bpt, &bno);
    lastinode->datablock[blkno] = bno;
    lastinode->datablock_pt[blkno] = bpt;
  }
  // 插入链表，建立父指针
  dentry->nxt = lastinode->dentry_sons;
  lastinode->dentry_sons = dentry;
  dentry->fa = lastdentry;

	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param xyffs_stat 返回状态
 * @return int 0成功，否则失败
 */
int xyffs_getattr(const char* path, struct stat * xyffs_stat) {
  int is_root = 0, is_find = 0;
  struct xyffs_dentry *dentry = parsepath(path, &is_root, &is_find);
  struct xyffs_inode *inode = dentry->inode_pt;
  if(!is_find)  return -ENOENT;
  if(dentry->type == T_DIR){
    xyffs_stat->st_mode = S_IFDIR | XYFFS_DEFAULT_PERM;
    xyffs_stat->st_size = inode->dircnt * sizeof(struct xyffs_dentry_d);
  }
  else if(dentry->type == T_FILE){
    xyffs_stat->st_mode = S_IFREG | XYFFS_DEFAULT_PERM;
    xyffs_stat->st_size = inode->size;
  }
  else{
    printf("\033[1;31mgetattr: incorrect file type!\033[0m\n");
  }
  
  xyffs_stat->st_nlink = 1;
  xyffs_stat->st_uid = getuid();
  xyffs_stat->st_gid = getgid();
  xyffs_stat->st_atime = time(NULL);
  xyffs_stat->st_mtime = time(NULL);
  xyffs_stat->st_blksize = 1024;
  
  if(is_root){
    xyffs_stat->st_size = 0; // TODO
    xyffs_stat->st_blocks = SZ_DISK / 1024;
    xyffs_stat->st_nlink = 2;
  }

	return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int xyffs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */
  int is_root, is_find;
  struct xyffs_dentry *dentry = parsepath(path, &is_root, &is_find);
  struct xyffs_dentry *subdentry;
  int cur_dir = offset;
  if(is_find){
    struct xyffs_inode *inode = dentry->inode_pt;
    subdentry = get_kth_subdentry(inode, cur_dir);
    if(subdentry)
      filler(buf, subdentry->name, NULL, ++offset);
    return 0;
  }
  return -ENOENT;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int xyffs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
  int is_root, is_find;
  struct xyffs_dentry *lastdentry = parsepath(path, &is_root, &is_find);
  struct xyffs_inode *lastinode = lastdentry->inode_pt;
  struct xyffs_dentry *dentry;
  
  if(is_find){
    printf("\033[1;31mFile name already exists!\033[0m\n");
    return -EEXIST;
  }
  
  char *fname = path2filename(path);
  if (S_ISREG(mode)){
    dentry = newdentry(fname, T_FILE, -1);
    allocinode(dentry, T_FILE);
  }
  else if(S_ISDIR(mode)){
    dentry = newdentry(fname, T_DIR, -1);
    allocinode(dentry, T_DIR);
  }
  // lastinode 加一个目录项
  int dirs_per_blk = 1024 / sizeof(struct xyffs_dentry_d);
  lastinode->dircnt++;
  if(lastinode->dircnt == 1 || ((lastinode->dircnt - 1) / dirs_per_blk != (lastinode->dircnt - 2) / dirs_per_blk)){
    // 需要新的数据块来存增加的目录项
    int blkno = (lastinode->dircnt - 1) / dirs_per_blk;
    if(blkno > 6){
      printf("\033[1;31mNo enough memory!\033[0m\n");
      return -1;
    }
    char *bpt; int bno;
    allocdatablock(bpt, &bno);
    lastinode->datablock[blkno] = bno;
    lastinode->datablock_pt[blkno] = bpt;
  }
  // 插入链表，设置父指针
  dentry->nxt = lastinode->dentry_sons;
  lastinode->dentry_sons = dentry;
  dentry->fa = lastdentry;
	return 0;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int xyffs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int xyffs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int xyffs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	return size;			   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int xyffs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int xyffs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int xyffs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int xyffs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int xyffs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int xyffs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则失败
 */
int xyffs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	xyffs_options.device = strdup("/home/guests/190110230/ddriver"); // TODO: 这里填写你的ddriver设备路径");

	if (fuse_opt_parse(&args, &xyffs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
