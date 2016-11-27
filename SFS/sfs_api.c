#include "sfs_api.h"
#include "disk_emu.h"


#define FILENAME "File System"
#define BLOCK_SIZE 1024
#define BLOCK_NUM 2000

//all the lengths are #blk
#define SUPER_BLOCK_LENGTH 1
#define INODE_TABLE_LENGTH 200	//can hold 199 files, the first one is reserved for directory
#define DATA_BLOCK_LENGTH 798
#define FREE_BIT_MAP_LENGTH 1

#define DIRECT_PTR_NUM 12
#define OPEN_FILE_TABLE_SIZE 199
#define MAX_FILE_BLK (DIRECT_PTR_NUM + BLOCK_SIZE/sizeof(int))

#define SUPER_BLOCK_INDEX 0
#define INODE_TABLE_INDEX SUPER_BLOCK_LENGTH
#define DATA_BLOCK_INDEX (INODE_TABLE_INDEX + INODE_TABLE_LENGTH)
#define FREE_BIT_MAP_INDEX (DATA_BLOCK_INDEX + DATA_BIT_MAP_LENGTH)

//====================data structure=====================
typedef struct super_block{
	int magic;
	int block_size;	//1024
	int file_system_size;	//#blk
	int inode_table_length;	//#blk
} super_block;

//64
typedef struct inode{
	int initialized;	//0 for uninitialized, 1 for initialized
	int link_cnt;
	int size;	//bytes
	int direct_ptr[DIRECT_PTR_NUM];	//block index
	int indirect_ptr;
} inode;


//open file table
typedef	struct file{
	//file descriptor = file ID = array index in open file table
	int inode_index;
	int readptr;
	int writeptr;
}file;

typedef struct open_file_table{
	file files[OPEN_FILE_TABLE_SIZE];
} open_file_table;






void mksfs(int fresh){

}
int sfs_get_next_file_name(char *fname){
  return 0;
}
int sfs_get_file_size(char* path){
  return 0;
}
int sfs_fopen(char *name){
  return 0;
}
int sfs_fclose(int fileID){
  return 0;
}
int sfs_frseek(int fileID, int loc){
  return 0;
}
int sfs_fwseek(int fileID, int loc){
  return 0;
}
int sfs_fwrite(int fileID, char *buf, int length){
  return 0;
}
int sfs_fread(int fileID, char *buf, int length){
  return 0;
}
int sfs_remove(char *file){
  return 0;
}








