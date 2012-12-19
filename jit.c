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


#define CODE_SIZE	0x8000
#define MEM_SIZE	30000
#define MAX_PROG_SIZE	3000
#define PP		0xFF

enum command{
	PTR_INC,
	PTR_DEC,
	OUTPUT, //doesn't do anything yet
	INPUT,
	VAL_INC,
	VAL_DEC,
	LOOP_START,
	LOOP_END,
	NOP
};

char msg[] = "%c"; //printf argument for output command


typedef struct _codegen_entry{
	enum command command;
	uint8_t instrs[42]; //opcodes for command
	uint8_t size; 
	uint8_t dp_linkedits[8]; //list of fixups for dp
	uint8_t num_dps;
	uint8_t mem_linkedits[8]; //list of fixups for dp
	uint8_t num_mems;
	uint8_t printf_arg; //needed by output (.)
	uint8_t target_offset; //used for jumps ([ and])
	} codegen_entry;
	
 
/*
Command to opcode mappings. PP = parameters overwritten when linking.
*/
const codegen_entry table[] = {
	{PTR_INC, {0xa1,PP,PP,PP,PP,		//mov eax, [dp]
		0x40,				//inc eax
		0xbb,PP,PP,PP,PP,		//mov ebx, dp
		0x89,0x03			//mov [ebx], eax
		}, 13, {1,7}, 2,{0},0, 0, 0},
	{PTR_DEC, {0xa1,PP,PP,PP,PP,		//mov eax, [dp]
		0x48,				//dec eax
		0xbb,PP,PP,PP,PP,		//mov ebx, dp
		0x89,0x03			//mov [ebx], eax
		}, 13, {1,7}, 2,{0},0, 0, 0},
		
	{OUTPUT, {0x8b,0x1d,PP,PP,PP,PP, 	//mov edx, [mem]
		0x8b,0x15,PP,PP,PP,PP, 		//mov ebx, [dp]
		0x81,0xec,0x04,0x00,0x00,0x00, 	//sub esp 0xc ;needed to meet OSX's stack alignment requirements
		0x8a,0x04,0x13, 		//mov eax, [edx + ebx]
		0x50, 				//push eax
		0x68,PP,PP,PP,PP, 		//push "%c"
		0xb8,PP,PP,PP,PP, 		//mov eax, printf
		0xff,0xd0, 			//jmp eax
		0x81,0xc4,0xc,0x00,0x00,0x00 	//add esp 0x14
		},40, {8}, 1, {2}, 1, 28, 23},
	{VAL_DEC, {0x8b, 0x1d, PP,PP,PP,PP, 	//mov edx, [mem]
		0x8b, 0x15, PP, PP, PP, PP, 	//mov ebx, [dp]
		0x8a, 0x04, 0x1a,		//mov al, [mem + dp]
		0xfe, 0xc8,			//dec al
		0x88, 0x04,0x1a			//mov [mem + dp], al
		}, 20, {8}, 1, {2}, 1, 0, 0},
	{VAL_INC, {0x8b, 0x1d, PP,PP,PP,PP,	//mov edx, [mem]
		0x8b, 0x15, PP, PP, PP, PP, 	//mov ebx, [dp]
		0x8a, 0x04, 0x1a,		//mov al, [mem+dp]
		0xfe, 0xc0,			//inc al
		0x88, 0x04,0x1a			//mov [mem + dp], al
		}, 20, {8}, 1, {2}, 1, 0, 0},
			
	{LOOP_START, {
		0x8b, 0x1d, PP, PP, PP, PP, 	//mov ebx, [mem]
		0x8b, 0x15, PP, PP, PP, PP, 	//mov edx, [dp]
		0x8a, 0x04, 0x13, 		//mov al [edx + ebx]
		0xbb, PP,PP,PP,PP, 		//mov ebx target
		0x84, 0xc0, 			//test al,al
		0x74, 0x02, 			//je loc1
		0xeb, 0x02, 			//jmp loc2
/*loc1:*/	0xff, 0xe3, 			//jmp ebx
/*loc2:*/	0x90 				//nop
	}, 29, {8}, 1, {2}, 1, 0, 16},
	{LOOP_END, {
		0x8b, 0x1d, PP, PP, PP, PP, 	//mov ebx, [mem]
		0x8b, 0x15, PP, PP, PP, PP, 	//mov edx, [dp]
		0x8a, 0x04, 0x13, 		//mov al [edx + ebx]
		0xbb, PP,PP,PP,PP, 		//mov ebx target
		0x84, 0xc0, 			//test al,al
		0x75, 0x02, 			//jne loc1
		0xeb, 0x02, 			//jmp loc2
/*loc1:*/	0xff, 0xe3, 			//jmp ebx
/*loc2:*/	0x90 				//nop
	}, 29, {8}, 1, {2}, 1, 0, 16}		
	/*NOP is missing as it's ignored by compile().	*/
};

/* Given a command, look it up in the above table: */
codegen_entry get_cmd(enum command type){
	for(int i = 0; i<(sizeof table/sizeof table[0]); i++){
		if(table[i].command == type){
			return table[i];
		}
	}
	printf("Invalid command\n");
	exit(-1);
}

/* Lookup types and table for tokeniser: */
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
int tokenise(const char* program, enum command* buf, int bufsize){
	assert(strlen(program) <= bufsize);
	int count = 0;
	while(*program != '\0'){
		*buf = get_token(*program);
		
		if(*buf != NOP){
			buf++;
			count++;
		}
		program++;
		
	}
	return count;
}


/* Writes the selected instruction at dest, patching any parts that
need patching. Writes the address of loop targets to the fixup parameter.
*/
int emit_instruction(enum command type, uint32_t* dp, uint8_t** mem, uint8_t* dest, uint32_t** fixup){
	uint8_t* linktarget;

	codegen_entry entry = get_cmd(type);
	
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
		*fixup = (uint32_t*)&dest[entry.target_offset];
	}else if(type == OUTPUT){
		linktarget = dest;
		linktarget += entry.target_offset;
		*((uint32_t*)linktarget) = (uint32_t)msg;
		
	}
	
	return entry.size;
	
}




LIST_HEAD(_loop_entry_head, _loop_entry) loop_entries;

typedef struct _loop_entry{ //tracks a matched pair of [ and ] commands
	int number; //tracks indentation level, used to match corresponding braces
	uint32_t* start_addr; //location of the first instruction of the current '['
	uint32_t* end_addr; //location of the first instruction of the matching ']'
	uint32_t* start_fixup; //points to argument of the JE in the loop
	uint32_t* end_fixup; //points to the argument of the JNE in the loop
	LIST_ENTRY(_loop_entry) entries;
	} loop_entry;

/*
Walks the list of commands, generates the code and writes loop_entries where needed.
Once code generation is done it walks the loop entries to fixup any args.

*/
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
		
		//not the cleanest code - fail AFTER overflowing the buffer.
		assert((count+sz) <= destsz);
		
		if(current == LOOP_START){
			loopnumber++; 
			loop_metadata = malloc(sizeof *loop_metadata);
			
			assert(loop_metadata != NULL);
			
			loop_metadata->number = loopnumber;
			LIST_INSERT_HEAD(&loop_entries, loop_metadata, entries);
				
			loop_metadata -> start_addr = (uint32_t*)(cur_target + (uint32_t)sz);
			loop_metadata -> start_fixup = fixup;
			
		}else if(current == LOOP_END){
			//pull the entry out of the list for this pair:
			LIST_FOREACH(loop_metadata, &loop_entries, entries){
				if(loop_metadata->number == loopnumber){
					break;
				}
			}
			
			assert(loop_metadata != NULL);
			
			loop_metadata -> end_addr = (uint32_t*)(cur_target + (uint32_t)sz);
			loop_metadata -> end_fixup = fixup;
			
			/* 
			Now the metadata is written there's no need to reference it by number.
			Set it to some out-of-band number so if the loop number is recycled
			things don't break:
			*/
			loop_metadata->number = -1;
			
			loopnumber--;
		}
		
		count += sz;
		
		
		cur_target += sz;
	}
	
	*cur_target= 0xc3; //Write the final ret instruction.
	
	count++; //account for the ret.
	
	assert(loopnumber == 0);
	
	/* Apply fixups to all loop pairs: */
	LIST_FOREACH(loop_metadata, &loop_entries, entries){
		
		*(loop_metadata -> start_fixup) = (uint32_t)loop_metadata -> end_addr;
		*(loop_metadata -> end_fixup) = (uint32_t)loop_metadata -> start_addr;
	}
	
	return count;
}


void run(void* buffer, size_t size){
	uint8_t* code = buffer;

	uint8_t* oldcode = code;
	code = code - ((unsigned long)code%getpagesize());
	
	size += (oldcode - code) ; //make sure size includes the amount shifted back above
	
	/* Asking for RWX as with different CODE_SIZEs memory and code can land on the same page: */
	if(mprotect(code,size,PROT_READ|PROT_WRITE|PROT_EXEC)){
		printf("Couldn't get RWX privs for page %p; errno= %x",code, errno);
		exit(-1);
	}


	void (*x)() = (void*)buffer;

	x();
}


int main (int argc, char** argv){
	//code will go here:
	uint8_t* targ = malloc(CODE_SIZE * sizeof *targ);
	
	/* Not strictly necessary, but makes error conditions easier to spot */
	for(int i=0; i < CODE_SIZE; i++){
		targ[i] = 0xCC; //int 0x3 ;software breakpoint
	}
	
	uint8_t* mem = calloc(1,MEM_SIZE * sizeof *mem);
	uint32_t dp = 0;

	enum command* cmdbuf = malloc(MAX_PROG_SIZE * sizeof *cmdbuf);
	int numcmds = tokenise(argv[1], cmdbuf, MAX_PROG_SIZE);
	
	assert(numcmds > 0);
	
	int codesize = compile(cmdbuf, numcmds, &mem, &dp, targ, CODE_SIZE);
	
	free(cmdbuf);

  	assert(codesize > 0);
	assert(codesize <= CODE_SIZE);

	run(targ, CODE_SIZE);
	
	free(mem);
	free(targ);
	
	fflush(stdout);
	return 0;
}