
	.thumb
	.global sheap_malloc_lr
	.global sheap_calloc_lr
	.global sheap_free_lr


sheap_malloc_lr:
	.func

	push       {r1, lr}
	mov        r1, lr
	bl         sheap_malloc
	pop        {r1, pc}

	.endfunc

sheap_calloc_lr:
	.func

	push       {r1, r2, lr}
	mov        r2, lr
	bl         sheap_calloc
	pop        {r1, r2, pc}

	.endfunc

sheap_free_lr:
	.func

	push       {r1, lr}
	mov        r1, lr
	bl sheap_free
	pop        {r1, pc}

	.endfunc


	.end
