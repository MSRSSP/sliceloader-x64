.text
.org 0

.globl realmode_entry
realmode_entry:
	.code16gcc
	cli
	jmp 1f

	# kernel entry point info; to be filled in by our loader
	.align 8
	.org 8
kernel_entry:
	.quad 0x5c3921544fd4ae2d # magic
kernel_arg:
	.quad 0

	# on entry, CS has an unknown base address, so we need to relocate everything
1:	xorl %ebx, %ebx
	mov %cs, %bx
	shll $4, %ebx
	addl %ebx, %cs:(gdt_descr_addr - realmode_entry)	# relocate GDT descriptor
	addl %ebx, %cs:(1f)									# relocate 32-bit jump target
	addl %ebx, %cs:(3f)									# relocate 64-bit jump target
	addl %ebx, %cs:(pml4 - realmode_entry)				# relocate PML4 entry

	# enter protected mode, load GDT
	mov $1, %eax
	mov %eax, %cr0
	lgdtl %cs:gdt_descr - realmode_entry

	# transition to 32-bit mode, paging disabled
	.byte 0x66, 0xea			# ljmpl indirect opcode
1:	.long 2f - realmode_entry	# offset (relocated above)
	.word 8					# segment selector

	.code32
2:	mov $0x18, %ax			# flat 32-bit data segment
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

    mov %cr4, %eax
    or $0x20, %eax			# set CR4.PAE
    mov %eax, %cr4

	# Fill PDPTE with 512 * 1GiB flat mappings
	mov $pdpte, %edi
	add %ebx, %edi			# relocate PDPTE
	mov $0x183, %eax        # G, PS, W, P
	xor	%esi, %esi
	mov $512, %ecx
1:	mov %eax, 0(%edi)
	mov %esi, 4(%edi)
	add $0x40000000, %eax
	adc	$0, %esi
	add $8, %edi
	dec %ecx
	jnz	1b

    mov $0xc0000080, %ecx
    rdmsr
    bts $8, %eax            # set MSR_IA32_EFER.LME
    wrmsr

	mov	$pml4, %eax
	add %ebx, %eax			# relocate PML4
    mov %eax, %cr3          # set CR3 = PML4

    mov %cr0, %eax
    bts $31, %eax           # set CR0.PG
    mov %eax, %cr0

	# enter 64-bit code, then jump to the kernel
	.byte 0xea				# ljmp opcode
3:	.long 1f - realmode_entry
	.word 16				# segment selector

	.code64
1:	mov kernel_arg(%rip), %rsi
	mov kernel_entry(%rip), %rax
	jmp *%rax

	.align 16
gdt:
	.quad 0
	.quad 0x00cf9b000000ffff // flat 32-bit code segment
	.quad 0x00af9b000000ffff // flat 64-bit code segment
	.quad 0x00cf93000000ffff // flat 32/64-bit data segment
gdt_end:

gdt_descr:
	.word gdt_end - gdt - 1    # size of GDT
gdt_descr_addr:
	.long gdt - realmode_entry # base of GDT (to be relocated)

	.balign 4096
pml4:
	.long pdpte + 0x23  # A, W, P
	.fill 0xffc, 1, 0

	.balign 4096
pdpte: .fill 0x1000, 1, 0
