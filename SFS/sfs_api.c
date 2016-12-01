#include "sfs_api.h"
#include "disk_emu.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
//FIXME: check if the the blk num exceed this limit when allocating new blk
#define MAX_FILE_BLK (DIRECT_PTR_NUM + BLOCK_SIZE/sizeof(int))
#define FREE_BIT_MAP_SIZE (DATA_BLOCK_LENGTH + 7)/8		//in bytes number
#define MAX_FILE_NAME_LEN 16
#define MAX_FILE_EXT_LEN 3
#define PTR_PER_BLK_NUM BLOCK_SIZE/sizeof(int)

#define SUPER_BLOCK_INDEX 0
#define INODE_TABLE_INDEX SUPER_BLOCK_LENGTH
#define DATA_BLOCK_INDEX (INODE_TABLE_INDEX + INODE_TABLE_LENGTH)
#define FREE_BIT_MAP_INDEX (DATA_BLOCK_INDEX + DATA_BLOCK_LENGTH)



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
	int blk_cnt;	//0 for no block, current block index = blk_cnt-1
	int size;	//bytes
	int direct_ptr[DIRECT_PTR_NUM];	//block index
	int indirect_ptr;
} inode;

typedef struct indirect_ptr_blk{
	int ptr[PTR_PER_BLK_NUM];
}indirect_ptr_blk;

//directory
typedef struct dir_item{
	char file_name[MAX_FILE_NAME_LEN + 1];	//one for null terminator
	char file_extension[MAX_FILE_EXT_LEN + 1];	//full name:file_name.file_extension
	int inode_index;
	int initialized;
	int visited;
}dir_item;

typedef struct dir{
	int file_num;
	int iterator;
	dir_item files[DIR_SIZE];
}dir;


//open file table
typedef	struct open_file_item{
	//file descriptor = file ID = array index in open file table = dir index
	int inode_index;	//default is 0, (0 is directory inode) == uninitialized
	int readptr;
	int writeptr;
}open_file_item;

typedef struct open_file_table{
	open_file_item files[OPEN_FILE_TABLE_SIZE];
} open_file_table;


//====================global (cached) variable==================
unsigned char free_bit_map[FREE_BIT_MAP_SIZE];	//1 for allocated, 0 for unallocated
inode inode_tableC[INODE_TABLE_LENGTH];
dir* dirC;
open_file_table* oft;


//methods definition
int sfs_fwseek(int fileID, int loc);
int init_dirC();
int update_disk_inode(int inode_index);


//=====================helper methods============================

//================free bit map helper method=====================
//FIXME: there may be garbage. BUT this may destroy data.

//initialize free_bit_map
void init_fbm(){
	void* buff = malloc(BLOCK_SIZE*FREE_BIT_MAP_LENGTH);
	if (read_blocks(FREE_BIT_MAP_INDEX, FREE_BIT_MAP_LENGTH, buff)<0){
		printf("ERROR in initializing free bit map.\n");
		printf("EXITING...\n");
		exit(-1);
	}
	memcpy(free_bit_map, buff, sizeof(unsigned char)*FREE_BIT_MAP_SIZE);
	free(buff);

}

//mark block #block_index as allocated in free bit map
void mark_as_allocated_in_fbm(int block_index){
	//FIXME: check if not unallocated

	unsigned char map = free_bit_map[block_index/8];
	unsigned char bit_mask = 128;
	int index = block_index%8;
	bit_mask = bit_mask>>index;
	map = map | bit_mask;
	free_bit_map[block_index/8] = map;
}

//mark block #block_index as unallocated in free bit map
void mark_as_unallocated_in_fbm(int block_index){
	//FIXME: check if not allocated

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
	printf("Disk full.\n");
	return -1;
}


void update_disk_fbm(){
	void* buf = malloc(BLOCK_SIZE);
	memcpy(buf, free_bit_map, sizeof(free_bit_map));
	write_blocks(FREE_BIT_MAP_INDEX, 1, buf);
	free(buf);
}


//=====================DIR=========================
void init_dir(int fresh){
	dirC = (dir*) malloc(sizeof(dir));

	//===================init dir inode
	//dir_item* dirC = (dir_item*) malloc(sizeof(dir_item)*DIR_SIZE);
	inode* dir_inode = (inode*) malloc(sizeof(inode));

	//retrieve old data
	void* buff = malloc(BLOCK_SIZE);
	if(read_blocks(INODE_TABLE_INDEX, 1, buff) <0){
		printf("Directory initialization Error.\n");
		free(buff);
		exit(-1);
	}
	memcpy(dir_inode, (dir*)buff, sizeof(inode));
		//FIXME: build dir_item cache table


	if (fresh == 1){
		(&(inode_tableC[0]))->link_cnt = 1;
		(&(inode_tableC[0]))->initialized = 1;

		//this is only  for fresh
		//init disk blk;
		int blk_cnt = (sizeof(dir) + BLOCK_SIZE -1)/BLOCK_SIZE;
		int i;
		for (i = 0; i<blk_cnt; i++){
			int blk_index = add_new_blk(&(inode_tableC[0])) - DATA_BLOCK_INDEX;
			if (blk_index < 0){
				printf("Error in initializing dir.\n");
				exit(-3);
			}
		}
		update_disk_fbm();
		update_disk_inode(0);
	}

	free(buff);
	if (fresh!= 1) init_dirC();
}

int init_dirC(){
	inode* dir_inode = &(inode_tableC[0]);
	int blk_cnt = dir_inode->blk_cnt;
	int rest = sizeof(dir) - (blk_cnt-1)*BLOCK_SIZE;
	int i;
	int blk_index;
	void* buf = malloc(BLOCK_SIZE);
	for (i = 0; i< blk_cnt-1; i++){
		blk_index = dir_inode->direct_ptr[i] + DATA_BLOCK_INDEX;
		read_blocks(blk_index, 1, buf);
		memcpy((void*)dirC + i*BLOCK_SIZE, buf, BLOCK_SIZE);
	}
	blk_index = dir_inode->direct_ptr[i] + DATA_BLOCK_INDEX;
	read_blocks(blk_index, 1, buf);
	memcpy((void*)dirC + i*BLOCK_SIZE, buf, rest);
	free(buf);
	return 0;
}


//return empty dir item index, or -1 if no empty dir entity left.
int find_unallocated_dirItem(){
	if (dirC->file_num == DIR_SIZE){
		printf("Maximum file number: 200. DISK FULL.\n");
		return -1;
	}
	int i;
	for (i =0; i<DIR_SIZE; i++){
		if ((&(dirC->files[i]))->initialized != 1){
			return i;
		}
	}
	printf("ERROR: Fail to find unallocated inode.\n");
	return -2;
}


//update dir item #dir_index, find the block which needs to be overwritten
void find_disk_blk_dir(int dir_index, int* start_b, int* end_b){
	size_t start = sizeof(dir_item)*dir_index + sizeof(int)*2; // two meta data
	size_t end = start +sizeof(dir_item);
	*start_b = start/BLOCK_SIZE;
	*end_b = end/BLOCK_SIZE;
}

//FIXME: need test
void update_disk_dir(int dir_index){
	int start_b;
	int end_b;
	void* buff = malloc(BLOCK_SIZE);
	void* start_p;
	int block_index;

	find_disk_blk_dir(dir_index, &start_b, &end_b);
	if (start_b == end_b){
		//write one page
		if (start_b != 0){	//if start_b == end_b == meta_b, no need to write anything
			//set up buffer to write to disk
			start_p = dirC + start_b*BLOCK_SIZE;
			memcpy(buff, start_p, BLOCK_SIZE);
			block_index = ((&(inode_tableC[0]))->direct_ptr)[start_b];
			write_blocks(DATA_BLOCK_INDEX + block_index, 1, buff);
		}
	}
	else{
		//write two page
		//write the second page
		start_p = dirC + end_b*BLOCK_SIZE;
		memcpy(buff, start_p, BLOCK_SIZE);
		block_index = ((&(inode_tableC[0]))->direct_ptr)[end_b];
		write_blocks(DATA_BLOCK_INDEX + block_index, 1, buff);

		//write first page if not the first block
		if(start_b != 0){	//start_b != 0, write two page
			start_p = dirC + start_b*BLOCK_SIZE;
			memcpy(buff, start_p, BLOCK_SIZE);
			block_index = ((&(inode_tableC[0]))->direct_ptr)[start_b];
			write_blocks(DATA_BLOCK_INDEX + block_index, 1, buff);
		}
	}

	//write meta data
	memcpy(buff, dirC, BLOCK_SIZE);
	block_index = ((&(inode_tableC[0]))->direct_ptr)[0];
	write_blocks(DATA_BLOCK_INDEX + block_index, 1, buff);

	free(buff);

}

//====================inode===========================

//return empty inode item index, or -1 if no empty inode left.
int find_unallocated_inode(){
	int i;
	for (i = 1; i< INODE_TABLE_LENGTH; i++){
		if((&inode_tableC[i])->initialized != 1){
			return i;
		}
	}
	printf("No empty inode. Disk FULL.\n");
	return -1;
}

void setup_inode_buffer(){
	void* buf = malloc(BLOCK_SIZE);
	int i;
	for (i = 0; i < INODE_TABLE_LENGTH; i++){
		read_blocks(i + INODE_TABLE_INDEX, 1, buf);
		memcpy(&(inode_tableC[i]), buf, sizeof(inode));
	}
}


int update_disk_inode(int inode_index){
	void* buf = malloc(BLOCK_SIZE);
	memcpy(buf, &(inode_tableC[inode_index]), sizeof(inode));
	if (write_blocks(INODE_TABLE_INDEX + inode_index, 1, buf)<0){
		return -1;
	}
	return 0;
}

//FIXME: replace stupid code with the two methods
//return blk_index in disk (absolute address), -1 if not the blk is not initialized
int get_blk_index(int i_blk_index, inode* finode){
	if (finode ->blk_cnt <= i_blk_index){
		return -1;
	}
	if (i_blk_index < DIRECT_PTR_NUM){
		return finode->direct_ptr[i_blk_index] + DATA_BLOCK_INDEX;
	}

	//indirect index;
	int ptr_blk_index = finode->indirect_ptr + DATA_BLOCK_INDEX;
	void* buf = malloc(BLOCK_SIZE);
	read_blocks(ptr_blk_index, 1, buf);
	indirect_ptr_blk* ip_blk = (indirect_ptr_blk*) malloc(sizeof(indirect_ptr_blk));
	memcpy(ip_blk, buf, sizeof(indirect_ptr_blk));
	//index in indirect_ptr_blk array
	int ip_blk_index = i_blk_index - DIRECT_PTR_NUM +1;
	int blk_index = ip_blk->ptr[ip_blk_index] +DATA_BLOCK_INDEX;
	free(buf);
	return blk_index;

}

//update inode and fbm after calling this method
//return absolute blk index in disk
int add_new_blk(inode* finode){
	int i_blk_index = finode->blk_cnt;	//new blk in inode array index
	int blk_index;

	if (i_blk_index >= MAX_FILE_BLK){
		printf("File maximum size reached.\n");
		return -1;
	}

	blk_index = find_free_block();
	if (blk_index < 0){
		//no free blk left
		return -2;
	}

	//direct ptr
	if (i_blk_index < DIRECT_PTR_NUM){
		finode->direct_ptr[i_blk_index] = blk_index;
	}

	//indirect ptr
	else{
		int ptr_blk_index = finode->indirect_ptr + DATA_BLOCK_INDEX;
		void* buf = malloc(BLOCK_SIZE);
		read_blocks(ptr_blk_index, 1, buf);
		indirect_ptr_blk* ip_blk = (indirect_ptr_blk*) malloc(sizeof(indirect_ptr_blk));
		memcpy(ip_blk, buf, sizeof(indirect_ptr_blk));
		int ip_blk_index = i_blk_index - DIRECT_PTR_NUM +1;
		ip_blk->ptr[ip_blk_index] = blk_index;
		//update ip_blk in disk
		memcpy(buf, ip_blk, sizeof(indirect_ptr_blk));
		write_blocks(ptr_blk_index, 1, buf);
		free(buf);
	}

	finode->blk_cnt ++;
	mark_as_allocated_in_fbm(blk_index);
	return blk_index + DATA_BLOCK_INDEX;
}


//return file ID, if not found -> -1.
int find_file(char *name){
	int i;
	int fileCnt = 0;
	char* full_name = (char*) malloc(sizeof(char)*(MAX_FILE_NAME_LEN + MAX_FILE_EXT_LEN + 2));

	for (i = 0; i<DIR_SIZE; i++){

		if(dirC->file_num <= fileCnt){
			printf("File %s not found.\n", name);
			return -1;
		}

		if ((&(dirC->files[i]))->initialized == 0) continue;

		strcpy(full_name, (&(dirC->files[i]))->file_name);
		strcat(full_name, ".");
		strcat(full_name, (&(dirC->files[i]))->file_extension);
		if (strcmp(full_name, name) == 0){
			//file found
			free(full_name);
			return i;
		}

		fileCnt++;
	}

	free(full_name);
	printf("File %s not found.\n", name);
	return -1;
}


//create new file
//return dir index.
int create_file(char *file_name, char *file_ext){
	int dir_index = find_unallocated_dirItem();
	//dir full
	if (dir_index < 0) return -1;

	int inode_index = find_unallocated_inode();
	if (inode_index < 0) return -1;	//if dir is not full, this should not be full

	//init inode
	(&(inode_tableC[inode_index]))->initialized = 1;
	(&(inode_tableC[inode_index]))->link_cnt = 1;
	(&(inode_tableC[inode_index]))->blk_cnt = 0;
	(&(inode_tableC[inode_index]))->size = 0;

	//write to disk
	void* buff = malloc(BLOCK_SIZE);
	memcpy(buff, &(inode_tableC[inode_index]), sizeof(inode));
	write_blocks(INODE_TABLE_INDEX + inode_index, 1, buff);

	//no need to update dir inode

	//add dir item
	dirC->file_num++;
	(&(dirC->files[dir_index]))->initialized = 1;
	(&(dirC->files[dir_index]))->inode_index = inode_index;
	(&(dirC->files[dir_index]))->visited = 0;
	strcpy((&(dirC->files[dir_index]))->file_name, file_name);
	strcpy((&(dirC->files[dir_index]))->file_extension, file_ext);

	//write dir update to disk;
	update_disk_dir(dir_index);

	return dir_index;
}



void create_oft_item(int file_ID){
	int inode_index = (&(dirC->files[file_ID]))->inode_index;
	int size = (&(inode_tableC[inode_index]))->size;

	(&(oft->files[file_ID]))->inode_index = inode_index;
	(&(oft->files[file_ID]))->readptr = 0;
	(&(oft->files[file_ID]))->writeptr = size;
}



//======================other helper methods=================
int check_file_name(char* name){
	if (strlen(name)>20) return -1;
	//FIXME: check for file name len & ext len
	return 1;
}



//this also handles file name check
int split_name(char* name, char* file_name, char* file_ext){


	char* name_buff = (char*) malloc(sizeof(char)*(MAX_FILE_NAME_LEN + MAX_FILE_EXT_LEN + 2));
	strcpy(name_buff, name);
	const char dot[2] = ".";
	char* token;
	token = strtok (name_buff, dot);

	strcpy(file_name, token);
	token = strtok(NULL, dot);

	//no dot found
	if (token == NULL){
		printf("Invalid file name.\n");
		free(name_buff);
		return -1;
	}

	strcpy(file_ext, token);
	free(name_buff);

	if (strlen(file_ext)>MAX_FILE_EXT_LEN || strlen(file_name)>MAX_FILE_NAME_LEN){
		printf("Invalid file name. \nFile name max len is %d, file extension max len is %d.\n", MAX_FILE_NAME_LEN, MAX_FILE_EXT_LEN);
		return -2;
	}
	return 1;


}



int write_part_blk_from_beginning(char* buff, int i_blk_index, int inode_index, int length){
	inode* finode = &(inode_tableC[inode_index]);
	int return_val;
	int blk_index = get_blk_idnex(i_blk_index, finode);

	if(blk_index < 0){
		blk_index = add_new_blk(finode);
		if (blk_index < 0){
			return -1;
		}
		return_val = 2;
	}

	char* buf = (char*) malloc(BLOCK_SIZE);
	memcpy(buf, buff, length);
	if (write_blocks(blk_index + DATA_BLOCK_INDEX, 1, buf) < 0){
			free(buf);
			return -1;
		}
	free(buf);
	return return_val;
}

//if return value is 2, inode and fbm needs to be updated in disk
int write_full_blk(char* buff, int i_blk_index, int inode_index){
	inode* finode = &(inode_tableC[inode_index]);
	int blk_index = get_blk_idnex(i_blk_index, finode);
	int return_val;

	if(blk_index < 0){
		blk_index = add_new_blk(finode);
		if (blk_index < 0){
			return -1;
		}
		return_val = 2;
	}

	if (write_blocks(blk_index + DATA_BLOCK_INDEX, 1, buff) < 0){
		return -1;
	}
	return return_val;
}

int write_part_blk_from_mid(int offset, char* buff, int blk_index, int length){
	char* buf = (char*) malloc(BLOCK_SIZE);
	if (read_blocks(blk_index, 1, buf) < 0){
		free(buf);
		return -1;
	}
	memcpy(offset+buf, buff, length);
	if (write_blocks(blk_index, 1, buf) < 0){
		free(buf);
		return -2;
	}
	free(buf);
	return 0;
}



int read_block(char* buf, int offset, int rest, inode* finode, int i_blk_index){
	//FIXME: add indirect pointer
	int blk_index = finode->direct_ptr[i_blk_index];
	char* buff = (char*) malloc(BLOCK_SIZE);
	read_blocks(blk_index + DATA_BLOCK_INDEX, 1, buff);
	memcpy(buf, buff+offset, BLOCK_SIZE - offset - rest);
	return 0;
}

//fresh == 1, create new disk
void mksfs(int fresh){
/**
	unsigned char* fbm = free_bit_map;	//1 for allocated, 0 for unallocated
	inode* it = inode_tableC;
	dir* dc = dirC;
**/
	int init_result;
	printf("==========INITIALIZING DISK==========\n");
	if (fresh == 1){
		init_result = init_fresh_disk(FILENAME, BLOCK_SIZE, BLOCK_NUM);
	}
	else{
		init_result = init_disk(FILENAME, BLOCK_SIZE, BLOCK_NUM);
	}

	if (init_result == -1){
		printf("==========EXITING============\n");
		return;
	}


	//================partition=======================

	printf("creating partition...\n");

	//super block
	super_block* sb = (super_block*) malloc(sizeof(super_block));
	sb->block_size = BLOCK_SIZE;	//#bytes
	sb->file_system_size = BLOCK_NUM;	//#blk
	sb->inode_table_length = INODE_TABLE_LENGTH;
	//what does magic means??????
	sb->magic = 1;
	write_blocks(SUPER_BLOCK_INDEX, SUPER_BLOCK_LENGTH, sb);
	printf("Super Block: %d block.\n", SUPER_BLOCK_LENGTH);



	//free bit map
	init_fbm();


	//FIXME: retrieve data already in block
	if (fresh != 1){
		setup_inode_buffer();
	}
	printf("Inode Table: %d blocks.\n",INODE_TABLE_LENGTH);


	//directory
	init_dir(fresh);
	oft = (open_file_table*) malloc(sizeof(open_file_table));

	printf("The file system only support overwriting, no inserting.\n");



	int a = sfs_fopen("asd.ds");
	char* context = malloc(3000);
	char* hey = malloc(5);
	strcpy(context,"abc");

	for (int i = 0;i<300; i++){
		strcat(context,"zxcvbnmasd");
	}
	sfs_fwrite(a, context, 100);

	sfs_fread(a, hey, 10);


}


//return file ID;
int sfs_get_next_file_name(char *fname){
	if (dirC->file_num == 0){
		printf("No file in dir.\n");
		return -2;
	}
	int i;
	if (dirC->file_num <= dirC->iterator){
		printf("End of directory!\n");
		printf("Resetting iterator...\n");

		dirC->iterator = 0;
		int cnt = 0;
		for (i = 0; i<DIR_SIZE; i++){
			if ((&(dirC->files[i]))->visited == 1){
				(&(dirC->files[i]))->visited = 0;
				cnt++;
				if (cnt >= dirC->file_num) break;
			}
		}
		return -1;
	}

	for (i = 0; i < DIR_SIZE; i++){
		if ((&(dirC->files[i]))->visited == 1){
			continue;
		}
		break;
	}

	strcpy(fname, (&(dirC->files[i]))->file_name);
	strcat(fname, ".");
	strcat(fname, (&(dirC->files[i]))->file_extension);


	(&(dirC->files[i]))->visited = 1;
	dirC->iterator +=1;

	//no need to write iterator & visited info into disk.
	return i;
}

//path is equal to fname
int sfs_get_file_size(char* path){
	int file_ID = find_file(path);
	if (file_ID < 0) return -1;

	int inode_index = (&(dirC->files[file_ID]))->inode_index;
	int size = (&(inode_tableC[inode_index]))->size;
	return size;
}


int sfs_fopen(char *name){

	char file_name[MAX_FILE_NAME_LEN + 1];
	char file_ext[MAX_FILE_EXT_LEN + 1];
	if (split_name(name, file_name, file_ext) < 0){
		//invalid name
		return -1;
	}

	int file_ID = find_file(name);

	//create new file
	if (file_ID == -1){
		printf("Creating file %s.\n", name);
		file_ID = create_file(file_name, file_ext);	//this creates file inode, updates dir
		if (file_ID < 0){
			printf("Error in opening file %s.", name);
		}
	}

	//create open file table entity
	create_oft_item(file_ID);
	return 0;
}


int sfs_fclose(int fileID){
  if ((&(oft->files[fileID]))->inode_index == 0){
	  printf("File #%d not opened.",fileID);
	  return -1;
  }
  //inode_index 0 is dir, so no file can have index 0 => 0 is uninitialized
  (&(oft->files[fileID]))->inode_index = 0;
  return 0;
}

int sfs_frseek(int fileID, int loc){
	int inode_index = (&(oft->files[fileID]))->inode_index;

	if (inode_index == 0){
		//not initialized
		printf("Invalid fileID.\n");
		return -1;
	}

	int size = (&(inode_tableC[inode_index]))->size;
	if (loc < 0 || loc > size){
		printf("Invalid location.\nLoc should be bigger than 0 and less than than the size of the file (%d).\n", size);
		return -1;
	}
	(&(oft->files[fileID]))->readptr = loc;
	return 0;
}

int sfs_fwseek(int fileID, int loc){
	int inode_index = (&(oft->files[fileID]))->inode_index;

	if (inode_index == 0){
		//not initialized
		printf("Invalid fileID.\n");
		return -1;
	}

	int size = (&(inode_tableC[inode_index]))->size;
	if (loc < 0 || loc > size){
		printf("Invalid location.\nLoc should be bigger than 0 and less than than the size of the file (%d).\n", size);
		return -1;
	}
	(&(oft->files[fileID]))->writeptr= loc;
	return 0;
}


int sfs_fwrite(int fileID, char *buf, int length){

	//FIXME: bug when buf is too big.
	//check in oft for fileID
	open_file_item* f_oft_item = &(oft->files[fileID]);
	int inode_index = f_oft_item->inode_index;
	if (inode_index == 0){
		printf("File %d not found.\n", fileID);
		return -1;
	}

	//offset = wp%blk_size
	int wp = f_oft_item->writeptr;
	int offset = wp % BLOCK_SIZE;
	int i_blk_index = wp/BLOCK_SIZE;
	inode* finode = &(inode_tableC[inode_index]);

	int blk_index = get_blk_index(i_blk_index, finode);
	int update = 0;

	if(offset + length <= 1024){
		if (offset == 0){
			if (write_part_blk_from_beginning(buf, i_blk_index, inode_index, length) == 2){
				update++;
			}
		}
		else {
			write_part_blk_from_mid(offset, buf, blk_index, length);
		}
		//change file size;
		f_oft_item->writeptr += length;
		if (finode->size >= f_oft_item->writeptr){
			return length;
		}
		finode->size = f_oft_item->writeptr;
		update_disk_inode(inode_index);
		if (update > 0) update_disk_fbm();
		return length;
	}


	write_part_blk_from_mid(offset, buf, blk_index, BLOCK_SIZE - offset);

	i_blk_index += 1;
	int rest_len = length;
	rest_len -= (BLOCK_SIZE - offset);
	int blk_num = rest_len / BLOCK_SIZE;
	char* bufp = buf + BLOCK_SIZE - offset;
	for(int i = 0; i<blk_num; i++){
		if (write_full_blk(bufp + i*BLOCK_SIZE, i_blk_index, inode_index)==2){
			update++;
			i_blk_index++;
		}
	}
	rest_len -= blk_num * BLOCK_SIZE;
	if (write_part_blk_from_beginning(bufp + blk_num*BLOCK_SIZE, i_blk_index, inode_index, rest_len) == 2){
		update++;
	}

	f_oft_item->writeptr += length;
	if (finode->size < f_oft_item->writeptr){
		finode->size = f_oft_item->writeptr;
		if (update <= 0)update_disk_inode(inode_index);
	}

	if(update > 0){
		update_disk_inode(inode_index);
		update_disk_fbm();
	}

	return length;
}

int sfs_fread(int fileID, char *buf, int length){

	open_file_item* f_oft_item = &(oft->files[fileID]);
	int inode_index = f_oft_item->inode_index;
	if (inode_index == 0){
		printf("File %d not found.\n", fileID);
		return -1;
	}

	int rp = f_oft_item->readptr;
	int offset = rp % BLOCK_SIZE;
	int i_blk_index = rp/BLOCK_SIZE;
	inode* finode = &(inode_tableC[inode_index]);

	int rest = BLOCK_SIZE - length - offset;
	if (rest>=0){
		//one block is enough
		read_block(buf, offset, rest, finode, i_blk_index);
	}

	else{
		read_block(buf, offset, 0, finode, i_blk_index);
		i_blk_index++;
		rest = abs(rest);
		char* bufp = length - rest + buf;
		int blk_num = rest/BLOCK_SIZE;
		rest -= blk_num * BLOCK_SIZE;

		int i;
		for(i = 0; i<blk_num; i++){
			read_block(bufp + i*BLOCK_SIZE, 0, 0, finode, i_blk_index);
			i_blk_index++;
		}
		if (rest > 0){
			read_block(bufp + i*BLOCK_SIZE, 0, rest, finode, i_blk_index);
		}
	}
	f_oft_item->readptr += length;
	return 0;
}


//FIXME: do we have to open the file before we remove it?
int sfs_remove(char *file){
	int file_ID = find_file(file);

	if (file_ID == -1){
		printf("File %s not found.\n", file);
		return -1;
	}

	//remove dir entry
	(&(dirC->files[file_ID]))->initialized = 0;
	//if opened, release oft
	int inode_index = (&(oft->files[file_ID]))->inode_index;
	(&(oft->files[file_ID]))->inode_index = 0;

	//release inode
	inode* finode = &(inode_tableC[inode_index]);
	finode->initialized = 0;

	//release data block
	int blk_cnt = finode->blk_cnt;
	int i;
	int blk_index;
	for(i = 0; i<blk_cnt; i++){
		blk_index = finode->direct_ptr[i];
		mark_as_unallocated_in_fbm(blk_index);
	}

	return 0;
}








