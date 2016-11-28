#include "sfs_api.h"
#include "disk_emu.h"
#include <stdio.h>
#include <math.h>

#define FILENAME "File System"
#define BLOCK_SIZE 1024
#define BLOCK_NUM 2000

//all the lengths are #blk
#define SUPER_BLOCK_LENGTH 1
#define INODE_TABLE_LENGTH 200	//can hold 199 files, the first one is reserved for directory
#define DATA_BLOCK_LENGTH 1798
#define FREE_BIT_MAP_LENGTH 1

#define DIRECT_PTR_NUM 12
#define DIR_SIZE 199
#define OPEN_FILE_TABLE_SIZE 199
#define MAX_FILE_BLK (DIRECT_PTR_NUM + BLOCK_SIZE/sizeof(int))
#define FREE_BIT_MAP_SIZE (DATA_BLOCK_LENGTH + 7)/8		//in bytes number

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


//64 bytes per inode
typedef struct inode{
	int initialized;	//0 for uninitialized, 1 for initialized
	int link_cnt;
	int size;	//bytes
	int direct_ptr[DIRECT_PTR_NUM];	//block index
	int indirect_ptr;
} inode;


//directory
typedef struct dir_item{
	char file_name[16];
	char file_extension[3];	//full name:file_name.file_extension
	int inode_index;
}dir_item;


//FIXME: this may be in cache
typedef struct dir{
	dir_item files[DIR_SIZE];
}dir;


//open file table
typedef	struct open_file_item{
	//file descriptor = file ID = array index in open file table
	int inode_index;
	int readptr;
	int writeptr;
}open_file_item;

typedef struct open_file_table{
	open_file_item files[OPEN_FILE_TABLE_SIZE];
} open_file_table;


//====================global (cached) variable==================
unsigned char free_bit_map[FREE_BIT_MAP_SIZE];	//1 for allocated, 0 for unallocated



//=====================helper methods============================

//FIXME: there may be garbage. BUT this may destroy data.
//initialize free_bit_map
void initialize_fbp(){
	int i = 0;
	for (i = 0; i<FREE_BIT_MAP_SIZE; i++){
		free_bit_map[i] = 0;
	}
}

//mark block #block_index as allocated in free bit map
void mark_as_allocated_in_fbm(int block_index){
	//check if not unallocated

	unsigned char map = free_bit_map[block_index/8];
	unsigned char bit_mask = 128;
	int index = block_index%8;
	bit_mask = bit_mask>>index;
	map = map | bit_mask;
	free_bit_map[block_index/8] = map;
}

//mark block #block_index as unallocated in free bit map
void mark_as_unallocated_in_fbm(int block_index){
	//check if not allocated

	unsigned char map = free_bit_map[block_index/8];
	int index = block_index%8;
	unsigned char bit_mask = 256 -1 - (int)pow(2.0, 7.0-index);
	map = map & bit_mask;
	free_bit_map[block_index/8] = map;
}

//return the free block index
//if no free block left, return -1
int find_free_block(){
	int i;
	int j;
	for (i = 0; i<FREE_BIT_MAP_SIZE; i++){
		unsigned char map = free_bit_map[i];
		if (map == 255){	//map == 11111111 ==>the eight blocks in this map are all allocated
			continue;
		}
		unsigned char bit_mask = 128;
		for (j = 0; j<8; j++){
			if ((map & bit_mask) == 0){
				//found empty block
				return 8*i+j;
			}
			bit_mask = bit_mask>>1;
		}
	}
	return -1;
}

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








