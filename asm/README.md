# Sheap Assembler Functions

The sheap allocator provides an extended memory layout. This layout can store an additional four byte value (uint32_t) into the memory header. This can be used to store the origin of the allocation or deallocation.
The default allocation and free functions (```sheap_malloc_lr```, ```sheap_calloc_lr``` and ```sheap_free_lr```) are implemented in assembler. As compilers do not necessarily share commong assembler directives, one may need to copy and adjust the available ```.asm``` file for a specific compiler/assembler.

This directory contains two ```.asm``` files. The ```sheap_alloc_gcc.asm``` file provides the ```gcc``` implementation. The ```sheap_alloc_ticlang.asm_``` provides a ```ticlang``` implementation. (Change the trailing file ending to use a different ```.asm``` file) 

The following links provide information about the ```gcc``` and ```ticlang``` assembler directives. [arm_gnu], [arm_ticlang]

Sample ```gcc``` implementation with comments:
```assembly
.thumb
    .global sheap_malloc_lr
	.global sheap_calloc_lr
	.global sheap_free_lr


sheap_malloc_lr:
	.func

	push       {r1, lr}         ;store r1 as we change it ;store lr to return
    mov        r1, lr           ;mov lr to r1 - r1 is the second parameter for the sheap_malloc call which is used to store the origin of the caller
    bl         sheap_malloc     ;branch and link to the C function
    pop        {r1, pc}         ;restore the stored values

	.endfunc

sheap_calloc_lr:
	.func

	push       {r1, r2, lr}     ;same as malloc, just an addition parameter to store
    mov        r2, lr
    bl         sheap_calloc
    pop        {r1, r2, pc}

	.endfunc

sheap_free_lr:
	.func

	push       {r1, lr}         ;same as malloc
    mov        r1, lr
    bl sheap_free
    pop        {r1, pc}

	.endfunc


	.end

```


   [arm_gnu]: <https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/useful-assembler-directives-and-macros-for-the-gnu-assembler>
   [arm_ticlang]: <https://www.ti.com/lit/pdf/spnu118>
