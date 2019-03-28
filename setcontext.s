        .syntax unified
	.global xsetcontext
	.text

	MCONTEXT_ARM_R4=48
	MCONTEXT_ARM_SP=84
	MCONTEXT_ARM_LR=88
	MCONTEXT_ARM_PC=92
	MCONTEXT_ARM_R0=32
	/* int setcontext (const ucontext_t *ucp) */
	
xsetcontext:
#if defined(__arm__)
	mov        r4, r0
	
	/* Loading r0-r3 makes makecontext easier.  */
	add     r14, r4, #MCONTEXT_ARM_R0
	ldmia   r14, {r0-r12}
	ldr     r13, [r14, #(MCONTEXT_ARM_SP - MCONTEXT_ARM_R0)]
	add     r14, r14, #(MCONTEXT_ARM_LR - MCONTEXT_ARM_R0)
	ldmia   r14, {r14, pc}
#elif defined(__x86_64__)
#endif
