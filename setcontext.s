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
	.global xsetcontext
	.text

	MCONTEXT_ARM_R4=48
	MCONTEXT_ARM_SP=84
	MCONTEXT_ARM_LR=88
	MCONTEXT_ARM_PC=92
	MCONTEXT_ARM_R0=32
	/* int setcontext (const ucontext_t *ucp) */
	
xsetcontext:
	mov        r4, r0
	
	/* Loading r0-r3 makes makecontext easier.  */
	add     r14, r4, #MCONTEXT_ARM_R0
	ldmia   r14, {r0-r12}
	ldr     r13, [r14, #(MCONTEXT_ARM_SP - MCONTEXT_ARM_R0)]
	add     r14, r14, #(MCONTEXT_ARM_LR - MCONTEXT_ARM_R0)
	ldmia   r14, {r14, pc}
#elif defined(__x86_64__)
	.global xsetcontext
	.text
xsetcontext:	
	pushq	%rdi
	
#if 0
	/* set signal mask */
	leaq	0x128(%rdi), %rsi
	xorl	%edx, %edx
	movl	$0x2, %edi
	movl	$0x8, %r10d
	movl	$0xe, %eax
	syscall
#endif

	popq	%rdi
	/* skip error checking */

	/* restore floating point */
	movq	0xe0(%rdi),%rcx
	fldenv	(%rcx)
	ldmxcsr	0x1c0(%rdi)

	movq	0xa0(%rdi), %rsp
	movq	0x80(%rdi), %rbx
	movq	0x78(%rdi), %rbp
	movq	0x48(%rdi), %r12
	movq	0x50(%rdi), %r13
	movq	0x58(%rdi), %r14
	movq	0x60(%rdi), %r15
	movq	0xa8(%rdi), %rcx
	push	%rcx
	movq	0x70(%rdi), %rsi
	movq	0x88(%rdi), %rdx
	movq	0x98(%rdi), %rcx
	movq	0x28(%rdi), %r8
	movq	0x30(%rdi), %r9
	movq	0x68(%rdi), %rdi
	xorl	%eax,%eax
	ret
#endif
