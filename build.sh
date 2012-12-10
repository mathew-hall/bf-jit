#!/bin/sh

NAME=bf

rm $NAME
rm $NAME.o


#use -f elf on linux.
nasm -f macho -l $NAME.lst $NAME.asm
gcc -mdynamic-no-pic -Wl,-no_pie -m32 -arch=i386 -o $NAME $NAME.o