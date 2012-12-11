#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

#define PP 0xFF

enum command{
	PTR_INC,
	PTR_DEC,
	OUTPUT,
	INPUT,
	VAL_INC,
	VAL_DEC,
	START_LOOP,
	END_LOOP
};
char msg[] = "%c";
typedef struct _codegen_entry{
	enum command command;
	uint8_t instrs[42]; 
	uint8_t size;
	uint8_t dp_linkedits[8];
	uint8_t num_dps;
	uint8_t mem_linkedits[8];
	uint8_t num_mems;
	uint8_t printf_arg;
	uint8_t target_offset;
	} codegen_entry;

 
const codegen_entry table[] = {
	{PTR_INC, {0xa1,PP,PP,PP,PP,0x40,0xbb,PP,PP,PP,PP,0x89,0x03,0xc3}, 14, {1,7}, 2,{0},0, 0, 0},
	{PTR_DEC, {0xa1,PP,PP,PP,PP,0x48,0xbb,PP,PP,PP,PP,0x89,0x03,0xc3}, 14, {1,7}, 2,{0},0, 0, 0},
		
	{OUTPUT, {0x8b,0x1d,PP,PP,PP,PP,
		0x8b,0x15,PP,PP,PP,PP,
		0x81,0xec,0x0c,0x00,0x00,0x00,
		0x8a,0x04,0x13,
		0x50,
		0x68,PP,PP,PP,PP,
		0xb8,PP,PP,PP,PP,
		0xff,0xd0,
		0x81,0xc4,0x14,0x00,0x00,0x00,
		0xc3
		},41, {8}, 1, {2}, 1, 28, 23},
			
	{VAL_INC, {0x8b, 0x1d, PP,PP,PP,PP, 0x8b, 0x15, PP, PP, PP, PP, 0xfe, 0x0c, 0x1a,0xc3}, 16, {8}, 1, {2}, 1, 0, 0},
	{VAL_DEC, {0x8b, 0x1d, PP,PP,PP,PP, 0x8b, 0x15, PP, PP, PP, PP, 0xfe, 0x04, 0x1a,0xc3}, 16, {8}, 1, {2}, 1, 0, 0}
};

codegen_entry get_cmd(enum command type){
	for(int i = 0; i<sizeof table; i++){
		if(table[i].command == type){
			return table[i];
		}
	}
	printf("Invalid command\n");
	exit(-1);
}

int emit_instruction(enum command type, uint32_t* dp, uint8_t** mem, uint8_t* dest, uint32_t** fixup){
	uint8_t* linktarget;
	puts("Getting cmd");
	codegen_entry entry = get_cmd(type);
	printf("Have an entry: %d bytes, %d dp fixups, %d mem fixups\n", entry.size, entry.num_dps,entry.num_mems);
	
	for(int i = 0; i<entry.size; i++){
		dest[i] = entry.instrs[i];
	}
	
	puts("Fixing up link target for dp");
	
	for(int i = 0; i<entry.num_dps; i++){
		linktarget = dest;
		linktarget += entry.dp_linkedits[i];
		
		*((uint32_t*)linktarget) = (uint32_t)dp;
	}
	
	puts("Successfully fixed up dp refs");
	
	for(int i = 0; i<entry.num_mems; i++){
		linktarget = dest;
		linktarget += entry.mem_linkedits[i];
		*((uint32_t*)linktarget) = (uint32_t)mem;
		
	}
	
	puts("Fixed up mem refs");
	
	if(entry.printf_arg != 0){
		linktarget = dest;
		linktarget += entry.printf_arg;
		*((uint32_t*)linktarget) = (uint32_t)printf;
	}
	puts("Targetted printf");
	if(entry.target_offset != 0 && type != OUTPUT){
		puts("Setting fixup");
		*fixup = (uint32_t*)&dest[entry.target_offset];
	}else if(type == OUTPUT){
		linktarget = dest;
		linktarget += entry.target_offset;
		*((uint32_t*)linktarget) = (uint32_t)msg;
		
	}
	puts("Fixup set");
	
	return entry.size;
	
}
void hexdump(void* ary, int num){
	uint8_t* a = (uint8_t*)ary;
	while(num--){
		printf("0x%0x ", *a++);
		if(num % 16 == 0)
			printf("\n");
	}
	
}



int main (int argc, char** argv){
	uint8_t* targ = malloc(500 * sizeof *targ);
	
	uint8_t* mem = malloc(30000 * sizeof *mem);
	
	uint32_t ip = 0;
	
	*mem = 0x42;
	
	int ret = emit_instruction(OUTPUT, &ip, &mem, targ, NULL);
	
	hexdump(targ, 40);
	
        unsigned long page_size = getpagesize();
  
	uint8_t* code = targ;

        printf("Code @ %p\n",code);
        code = code - ((unsigned long)code%getpagesize());
        printf("mprotecting page at %p\n",code);
        if(mprotect(code,131,PROT_READ|PROT_EXEC)){
          printf("Ah nuts. %x",errno);
          exit(-1);
        }


        void (*x)() = (void*)targ;
        printf("Trying to call shellcode\n");
        x();
	
	return 0;
}