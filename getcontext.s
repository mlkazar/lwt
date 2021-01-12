/*

This file was created by disassembling software covered by the Library GPL.

Copyright 2016-2020 Cazamar Systems

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

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
	/* basic registers */
	movq	%rbx, 0x80(%rdi)
	movq	%rbp, 0x78(%rdi)
	movq	%r12, 0x48(%rdi)
	movq	%r13, 0x50(%rdi)
	movq	%r14, 0x58(%rdi)
	movq	%r15, 0x60(%rdi)
	movq	%rdi, 0x68(%rdi)
	movq	%rsi, 0x70(%rdi)
	movq	%rdx, 0x88(%rdi)
	movq	%rcx, 0x98(%rdi)
	movq	%r8, 0x28(%rdi)
	movq	%r9, 0x30(%rdi)
	movq	(%rsp), %rcx
	movq	%rcx, 0xa8(%rdi)
	leaq	8(%rsp), %rcx
	movq	%rcx, 0xa0(%rdi)

	/* deal with FP registers */
	leaq	0x1a8(%rdi), %rcx
	movq	%rcx, 0xe0(%rdi)
	fnstenv	(%rcx)
	fldenv	(%rcx)
	stmxcsr	0x1c0(%rdi)

	/* signal mask */
#if 0
	leaq	0x128(%rdi), %rdx
	xorl	%esi,%esi
	xorl	%edi,%edi
	movl	$0x8,%r10d
	movl	$0xe, %eax
	syscall
#endif

	/* skip checking error */
	xorl	%eax,%eax
	ret
#endif	
