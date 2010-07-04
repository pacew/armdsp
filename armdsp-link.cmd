--rom_model
--heap  0x100000
--stack 0x100000
--warn_sections

MEMORY
{
	VECS: o = 0x80000000 l = 0x1000
	RAM: o = 0xc4000000 l = 0x03000000
	STACK: o = 0xc7000000 l 0x01000000
} 

SECTIONS
{
	.vecs > VECS

	.text > RAM

	GROUP (NEARDP)
	{
		.neardata
		.rodata
		.bss
	}

	.const > RAM
	.cio > RAM
	.cinit > RAM

	.far > RAM
	.fardata > RAM

	.sysmem > RAM

	.stack > STACK
}
