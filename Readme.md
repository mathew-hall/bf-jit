# A JITting Brainfuck Interpreter


##What?
Brainfuck is a very minimal programming language which models a (limited) Turing Machine. It has a very small set of commmands which makes it an ideal target for a toy compiler project, like this one.

##Why?
This was inspired by [Erik's post](http://blog.dubbelboer.com/2012/11/18/brainfuck-jit.html), as well as my interest in building some form of a compiler. This work follows on from the assembler I started building for the early version of Notch's DCPU.

##How?

1. Parse the program
2. Generate code
	1. Pull blocks out of a look-up-table
	2. Construct fixup table
3. Link jumps and printf calls
4. `mprotect` the code buffer to make it executable
5. Jump to it

### Running:

1. Build (with `buildjit.sh`)
2. Run with a brainfuck program in `argv[1]`, e.g. `./jit '>+++++++++[<++++++++>-]<.>+++++++[<++++>-]<+.+++++++..+++.[-]>++++++++[<++++>-]<.>+++++++++++[<++++++++>-]<-.--------.+++.------.--------.[-]>++++++++[<++++>-]<+.[-]++++++++++.'` (From Speedy's implementation over at [helloworld.org](http://www.helloworld.org))

##Issues

* Doesn't support input, yet.
* Handling of invalid commands (AKA comments) is brittle - spaces break it, for instance.
* Isn't doing anything particularly clever. Since the IR is a list with some metadata for loops it's not possible to do any fancy optimisations. 
* The lookup-table codegen approach is klunky. Since the code blocks are fixed, smart things like register allocation aren't possible. I guess since block-level info is available it should be feasible to deduce where fragments that repeatedly load & store the same cell could be exchanged for ones that don't waste this time.
* It doesn't bounds-check the pointer, so it's vulnerable to memory corruption attacks.