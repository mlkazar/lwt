#if defined(__arm__)
	.syntax unified
	.global xgetcontext

	.text
	
	/* int getcontext (ucontext_t *ucp) */
	
xgetcontext: 
	MCONTEXT_ARM_R4=48
	MCONTEXT_ARM_SP=84
	MCONTEXT_ARM_LR=88
	MCONTEXT_ARM_PC=92
	MCONTEXT_ARM_R0=32

	/* No need to save r0-r3, d0-d7, or d16-d31.  */
	add        r1, r0, #MCONTEXT_ARM_R4
	stmia   r1, {r4-r11}
	
	/* Save R13 separately as Thumb can't STM it.  */
	str     r13, [r0, #MCONTEXT_ARM_SP]
	str     r14, [r0, #MCONTEXT_ARM_LR]
	/* Return to LR */
	str     r14, [r0, #MCONTEXT_ARM_PC]
	/* Return zero */
	mov     r2, #0
	str     r2, [r0, #MCONTEXT_ARM_R0]
	
	/* Save ucontext_t * across the next call.  */
	mov        r4, r0
	
	/* Restore the clobbered R4 and LR.  */
	ldr        r14, [r4, #MCONTEXT_ARM_LR]
	ldr        r4, [r4, #MCONTEXT_ARM_R4]
	
	mov        r0, #0
	
	bx r14
#elif defined(__x86_64__)
	.global xgetcontext
	.text
xgetcontext:	
#endif	
