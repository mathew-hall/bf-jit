; Assembly listing, used to generate opcodes - doesn't actually play a role anymore.


%macro jumpifzero 1
	mov ebx, [prog]
	mov edx, [dp]
	
	mov eax, [ebx + edx]
	test eax, eax
	je %1
%endmacro

%macro jumpifnzero 1
	mov ebx, [prog]
	mov edx, [dp]
	
	mov byte al, [ebx + edx]
	mov ebx, %1
	test al, al
	jnz taken
	jmp short not_taken
taken:
	jmp ebx
not_taken:
	nop
%endmacro


	extern _printf
	
	
	SECTION .data
somestring:	db "hi world",0
charout:	db "%c",0

	SECTION .text
	global _main
	
deceval:
	mov ebx, [prog]
	mov edx, [dp]
	mov al, [edx + ebx]
	dec al
	mov byte [edx + ebx], al

	ret
	
inceval:
	mov ebx, [prog]
	mov edx, [dp]
	mov al, [edx + ebx]
	inc al
	mov byte [edx + ebx], al

	
	ret
	
incdp:
	mov eax, [dp]
	inc eax
	mov ebx, dp
	mov [ebx], eax
	
	ret
	
decdp:
	mov eax, [dp]
	dec eax
	mov ebx, dp
	mov [ebx], eax
	ret
	
	
printout:
	;stack is at 0x********4

	mov ebx, [prog]
	mov edx, [dp]
	
	sub esp, 0xC; OSX-specific alignment
	mov al, byte [ebx + edx]
	push eax 
	push charout 
	mov eax, dword 0xF00FF00F
	call eax
	
	add esp, 0x14

	ret


_main:	;stack =   0x*******C
	push ebp ; 0x*******8
	mov ebp, esp

	sub esp, 0x30
	
	mov eax, prog
	mov [eax], esp
	mov byte [esp], 0x49

	mov edx, dp
	mov dword [edx], 0
	
loops:
	call printout
	call deceval
	jumpifnzero loops

	
	add esp,0x30
	
	pop ebp
	ret

	
	SECTION .bss
	
dp resw 2
prog resw 2

