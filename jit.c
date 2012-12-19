#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/queue.h>
#include <signal.h>


#define CODE_SIZE 8192
#define PP 0xFF

enum command{
	PTR_INC,
	PTR_DEC,
	OUTPUT,
	INPUT,
	VAL_INC,
	VAL_DEC,
	LOOP_START,
	LOOP_END,
	NOP
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
	{PTR_INC, {0xa1,PP,PP,PP,PP,0x40,0xbb,PP,PP,PP,PP,0x89,0x03}, 13, {1,7}, 2,{0},0, 0, 0},
	{PTR_DEC, {0xa1,PP,PP,PP,PP,0x48,0xbb,PP,PP,PP,PP,0x89,0x03}, 13, {1,7}, 2,{0},0, 0, 0},
		
	{OUTPUT, {0x8b,0x1d,PP,PP,PP,PP, //mov edx, [dp]
		0x8b,0x15,PP,PP,PP,PP, //mov ebx, [mem]
		0x81,0xec,0x04,0x00,0x00,0x00, //sub esp 0xc
		0x8a,0x04,0x13, //mov eax, [edx + ebx]
		0x50, //push eax
		0x68,PP,PP,PP,PP, //push "%c"
		0xb8,PP,PP,PP,PP, //mov eax, printf
		0xff,0xd0, //jmp eax
		0x81,0xc4,0xc,0x00,0x00,0x00//, //add esp 0x14
		//0xc3 //ret
		},40, {8}, 1, {2}, 1, 28, 23},
	//VAL changes are suspect as they overflow to other bits of memory. Switch to load, modify reg, writeback.
	{VAL_DEC, {0x8b, 0x1d, PP,PP,PP,PP, 
		0x8b, 0x15, PP, PP, PP, PP, 
		0x8a, 0x04, 0x1a,
		0xfe, 0xc8,
		0x88, 0x04,0x1a}, 20, {8}, 1, {2}, 1, 0, 0},
	{VAL_INC, {0x8b, 0x1d, PP,PP,PP,PP, 
		0x8b, 0x15, PP, PP, PP, PP, 
		0x8a, 0x04, 0x1a,
		0xfe, 0xc0,
		0x88, 0x04,0x1a}, 20, {8}, 1, {2}, 1, 0, 0},
			
	{LOOP_START, {
		0x8b, 0x1d, PP, PP, PP, PP, //mov ebx, dp
		0x8b, 0x15, PP, PP, PP, PP, //mov edx, mem
		0x8a, 0x04, 0x13, //mov eax [edx + ebx] *** THIS NEEDS TO BE mov byte eax [edx + ebx]
		0xbb, PP,PP,PP,PP, //mov ebx target
		0x84, 0xc0, //test eax,eax
		0x74, 0x02, //je $$$$1
		0xeb, 0x02, //jmp $$$$2
		0xff, 0xe3, //$$$$1: jmp ebx
		0x90 //$$$$2: nop
	}, 29, {8}, 1, {2}, 1, 0, 16},
	{LOOP_END, {
		0x8b, 0x1d, PP, PP, PP, PP, //mov ebx, dp
		0x8b, 0x15, PP, PP, PP, PP, //mov edx, mem
		0x8a, 0x04, 0x13, //mov eax [edx + ebx]
		0xbb, PP,PP,PP,PP, //mov ebx target
		0x84, 0xc0, //test eax,eax
		0x75, 0x02, //jne $$$$1
		0xeb, 0x02, //jmp $$$$2
		0xff, 0xe3, //$$$$1: jmp ebx
		0x90 //$$$$2: nop
	}, 29, {8}, 1, {2}, 1, 0, 16}		
			
};

codegen_entry get_cmd(enum command type){
	for(int i = 0; i<(sizeof table/sizeof table[0]); i++){
		if(table[i].command == type){
			return table[i];
		}
	}
	printf("Invalid command\n");
	exit(-1);
}

typedef struct _token_entry{
	char token;
	enum command command;
	} token_entry;

const token_entry tokenLUT[] = {
	{'<', 	PTR_DEC	   },
	{'>', 	PTR_INC    },
	{'.', 	OUTPUT     },
	{'+', 	VAL_INC    },
	{'-', 	VAL_DEC    },
	{'[', 	LOOP_START },
	{']', 	LOOP_END   }
};

enum command get_token(char in){
	for(int i = 0; i< sizeof tokenLUT; i++){
		if(tokenLUT[i].token == in){
			return tokenLUT[i].command;
		}
	}
	return NOP;
}



int emit_instruction(enum command type, uint32_t* dp, uint8_t** mem, uint8_t* dest, uint32_t** fixup){
	uint8_t* linktarget;
//	puts("Getting cmd");
	codegen_entry entry = get_cmd(type);
//	printf("Have an entry: %d bytes, %d dp fixups, %d mem fixups\n", entry.size, entry.num_dps,entry.num_mems);
	
	for(int i = 0; i<entry.size; i++){
		dest[i] = entry.instrs[i];
	}
	
	for(int i = 0; i<entry.num_dps; i++){
		linktarget = dest;
		linktarget += entry.dp_linkedits[i];
		
		*((uint32_t*)linktarget) = (uint32_t)dp;
	}
	
	for(int i = 0; i<entry.num_mems; i++){
		linktarget = dest;
		linktarget += entry.mem_linkedits[i];
		*((uint32_t*)linktarget) = (uint32_t)mem;
		
	}
	
	if(entry.printf_arg != 0){
		linktarget = dest;
		linktarget += entry.printf_arg;
		*((uint32_t*)linktarget) = (uint32_t)printf;
	}

	if(entry.target_offset != 0 && type != OUTPUT){
		puts("Setting jump fixup");
		*fixup = (uint32_t*)&dest[entry.target_offset];
	}else if(type == OUTPUT){
		linktarget = dest;
		linktarget += entry.target_offset;
		*((uint32_t*)linktarget) = (uint32_t)msg;
		
	}
	
	return entry.size;
	
}
void hexdump(void* ary, int num){
	uint8_t* a = (uint8_t*)ary;
	while(num--){
		printf("0x%02x ", *a++);
		if(num % 16 == 0)
			printf("\n");
	}
	
}

int tokenise(const char* program, enum command* buf, int bufsize){
	assert(strlen(program) <= bufsize);
	int count = 0;
	while(*program != '\0'){
		*buf = get_token(*program);
		
		if(*buf != NOP){
//			printf("%c, %d",*program, count);
			buf++;
			count++;
		}
		program++;
		
	}
	return count;
}

LIST_HEAD(_loop_entry_head, _loop_entry) loop_entries;

typedef struct _loop_entry{
	int number;
	uint32_t* start_addr;
	uint32_t* end_addr;
	uint32_t* start_fixup;
	uint32_t* end_fixup;
	LIST_ENTRY(_loop_entry) entries;
	} loop_entry;

int compile(enum command* buf, int bufsize, uint8_t** ram, uint32_t* dp, uint8_t* dest, int destsz){
	uint8_t* cur_target = dest;
	uint32_t* fixup = NULL;
	
	LIST_INIT(&loop_entries);
	
	int loopnumber = 0;
	loop_entry* loop_metadata;
	
	int count = 0;
	
	for(int i = 0; i< bufsize; i++){
		enum command current = buf[i];
		if(current == NOP)
			continue;
		int sz = emit_instruction(current, dp, ram, cur_target, &fixup);

		if(current == LOOP_START){
			loopnumber++;
			loop_metadata = malloc(sizeof *loop_metadata);
			assert(loop_metadata != NULL);
			loop_metadata->number = loopnumber;
			LIST_INSERT_HEAD(&loop_entries, loop_metadata, entries);
				
			loop_metadata -> start_addr = (uint32_t*)(cur_target + (uint32_t)sz);
			loop_metadata -> start_fixup = fixup;
			
		}else if(current == LOOP_END){
			LIST_FOREACH(loop_metadata, &loop_entries, entries){
				if(loop_metadata->number == loopnumber){
					break;
				}
			}
			
			assert(loop_metadata != NULL);
			
			loop_metadata -> end_addr = (uint32_t*)(cur_target + (uint32_t)sz);
			loop_metadata -> end_fixup = fixup;
			
			loopnumber--;
		}
		
		count++;
		
		cur_target += sz;
	}
	
	*cur_target= 0xc3; //ret
	
	count++;
	
	assert(loopnumber == 0);
	
	LIST_FOREACH(loop_metadata, &loop_entries, entries){
		
		*(loop_metadata -> start_fixup) = (uint32_t)loop_metadata -> end_addr;
		*(loop_metadata -> end_fixup) = (uint32_t)loop_metadata -> start_addr;
	}
	
	return count;
}


void run(void* buffer, size_t size){
	uint8_t* code = buffer;
        unsigned long page_size = getpagesize();

        code = code - ((unsigned long)code%getpagesize());
//        printf("mprotecting page %p\n",code);
	//Need R W and X because it's likely that code & data will be placed on the same page.
        if(mprotect(code,size,PROT_READ|PROT_WRITE|PROT_EXEC)){
          printf("Ah nuts. %x",errno);
          exit(-1);
        }


        void (*x)() = (void*)buffer;

        x();
}

void flushAndExit(){
	printf("\nQuitting...\n");
	fflush(stdout);
	exit(-1);
}

int main (int argc, char** argv){
	signal(SIGINT, flushAndExit);
	uint8_t* targ = malloc(CODE_SIZE * sizeof *targ);
	
	for(int i=0; i < CODE_SIZE; i++){
		targ[i] = 0xCC; //fill it with debug traps to make life easier in case we jump out.
	}
	
	uint8_t* mem = calloc(1,30000 * sizeof *mem);
	
	uint32_t ip = 0;
	
	//int ret = emit_instruction(OUTPUT, &ip, &mem, targ, NULL);
	
	
	
	enum command* cmdbuf = malloc(3000 * sizeof *cmdbuf);
	
	int numcmds = tokenise(argv[1], cmdbuf, 3000);
	assert(numcmds > 0);
	int codesize = compile(cmdbuf, numcmds, &mem, &ip, targ, CODE_SIZE);
	
	free(cmdbuf);
	cmdbuf = NULL;

  	assert(codesize > 0);
	assert(codesize < CODE_SIZE);
	run(targ, CODE_SIZE);
	
	return 0;
}