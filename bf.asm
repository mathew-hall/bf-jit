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
	
	mov eax, [ebx + edx]
	test eax, eax
	jne %1
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
	dec byte [edx + ebx]
	
	ret
	
inceval:
	mov ebx, [prog]
	mov edx, [dp]
	inc byte [edx + ebx]
	
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
	
	sub esp, 0xC; stupid alignment bullshit
	mov al, byte [ebx + edx]
	push eax 
	push charout ; c
	call _printf
	
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
	; compiler needs to insert jump targets for loops.
	; to do this in asm we need to count the [s, dec ]s. Store from/to pairs of pointers
	; then use computed indirect branch (jmp [eax])
	
	add esp,0x30
	
	pop ebp
	ret

	
	SECTION .bss
	
dp resw 2
prog resw 2
