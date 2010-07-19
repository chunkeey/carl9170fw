	.globl _jump_to_bootcode
	.type  _jump_to_bootcode, @function
	_jump_to_bootcode:
		mov.l eeprom_start, r0
		jmp @r0
eeprom_start:	.long 0x00000008
tmp:		.long
