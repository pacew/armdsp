	.nocmp
	
VEC_ENTRY .macro addr
	.global addr
	STW   B0,*--B15
	MVKL  addr,B0
	MVKH  addr,B0
	B     B0
	LDW   *B15++,B0
	NOP   2
	NOP
	NOP
	.endm

	.sect ".vecs"
	.global vector_table
	.global _c_int00
	
vector_table:	
	MVKL	_c_int00,B0
	MVKH	_c_int00,B0
	B	B0
	NOP
	NOP
	NOP
	NOP
	NOP

	VEC_ENTRY int_handler1
	VEC_ENTRY int_handler2
	VEC_ENTRY int_handler3
	VEC_ENTRY int_handler4
	VEC_ENTRY int_handler5
	VEC_ENTRY int_handler6
	VEC_ENTRY int_handler7
	VEC_ENTRY int_handler8
	VEC_ENTRY int_handler9
	VEC_ENTRY int_handler10
	VEC_ENTRY int_handler11
	VEC_ENTRY int_handler12
	VEC_ENTRY int_handler13
	VEC_ENTRY int_handler14
	VEC_ENTRY int_handler15
