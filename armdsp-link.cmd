/*
 * memory layout for a board with 128 Mbytes of DDR ram
 * 0xc0000000 arm (64 mbytes)
 * 0xc4000000 dsp (64 mbytes)
 * 0xc8000000 end
 *
 * the TI runtime library forces us to declare the malloc pool size
 * at link time, so take a guess that 16 Mbytes will be a useful amount
 *
 * a program that needs more dynamically allocated memory can either change
 * this file, or use memory between the end of the program and the stack
 * (without the help of malloc)
 * 
 * the starting point of the unused memory can be determined by:
 *   max(&_LOAD_END_, &_CINIT_END_)
 */
--heap 0x1000000

/*
 * the size declared here for the stack is not enforeced - it actually
 * starts at the top of memory and grows down as far as it wants
 */
--stack 0x1000

--rom_model
--warn_sections

MEMORY
{
	VECS: o = 0x80000000 l = 0x1000
	RAM: o = 0xc4000000 l = 0x04000000
} 

SECTIONS
{
	.vecs: PALIGN(8) > VECS

	GROUP
	{
		.text: PALIGN(32) 

		GROUP {
			/* these sections need to be together for near model */
			.neardata: PALIGN(8)
			.rodata: PALIGN(8)
			.bss: PALIGN(8)
		} PALIGN(8)

		.const: PALIGN(8)
		.data: PALIGN(8)
		.fardata: PALIGN(8)
		.init_array: PALIGN(8)

		.switch: PALIGN(8)
		.cio: PALIGN(8)

		.far: PALIGN(8)
		.ppdata: PALIGN(8)

		.sysmem: PALIGN(4096)

	} > RAM, END(_LOAD_END_)

	.cinit: PALIGN(8) > RAM, END(_CINIT_END_)

	.stack: PALIGN(32) > RAM (HIGH)
}
