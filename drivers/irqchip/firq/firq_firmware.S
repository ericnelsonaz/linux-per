@
@ This program is free software; you can redistribute it and/or
@ modify it under the terms of the GNU General Public License Version
@ 2 as published by the Free Software Foundation.
@
	.text

	.arm

	.align 5
	.global firq_fiq_handler
	.global firq_fiq_handler_end

@ this should get registered as the FIQ handler
@ the following registers should get preset
@ sp ... virtual address of OCRAM
@ ... in OCRAM, these need to be set
@ ... +0x00 ... GIC CPU regs virtual address
@ ... +0x04 ... FIQ number for the GIC, so it can do EOI
@ ... +0x08 ... EPIT2 regs virtual address
@ Also, in OCRAM, we use those values:
@ ... +0x10 ... FIQ counter

firq_fiq_handler:

	@ Load various registers from OCRAM, see ocram_setupdata
	ldr r8, [sp, #0x0]

	@ ACK the interrupt by reading IAR register
	ldr r11, [r8, #0xc]
@	dsb

	@ De-assert the interrupt on the peripheral side
	@ Optional: Wait for the de-assertion to really happen
	ldr r9, [sp, #0x8]
	mov r10, #0xff
	orr r10, #0xff00
	str r10, [r9, #0x8]
	mov r10, #1
	str r10, [r9, #0x4]



	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ DEMO: Handle the in-OCRAM FIQ counter @
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	ldr r9, [sp, #0x10]
	add r9, #1
	str r9, [sp, #0x10]



	@ EOI the interrupt by writing EOIR register
	ldr r11, [sp, #0x4]
	str r11, [r8, #0x10]
@	dsb

	@ Optional: Re-trigger the interrupt on the peripheral side

	@ return from FIQ
	subs pc, lr, #4
	.align 5

@ this is to make sure that the literals are part of the firmware blob
firq_fiq_handler_end:
	.type firq_fiq_handler, function
	.size firq_fiq_handler, . - firq_fiq_handler
