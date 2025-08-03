
extern SerialInit
extern SerialWriteChar
extern SerialWriteSpace
extern SerialWriteNewLine
extern SerialWriteString
extern SerialWriteHex32

%macro DbgOut 1
	mov		esi, ebp
	add		esi, %1 - StartAbsolute
	call	SerialWriteString
%endmacro

%macro Hx32Out 1
	mov		esi, ebp
	add		esi, %1 - StartAbsolute
	mov		eax, [esi]
	call	SerialWriteHex32
%endmacro

%macro ImmHx32Out 1
	mov		eax, %1
	call	SerialWriteHex32
%endmacro
