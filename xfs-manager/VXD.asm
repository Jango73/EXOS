
; VXD.ASM

.386

;--------------------------------------------------------------------------

VXD segment byte public use32 "CODE"

  public _GetVXDEntry@4

;--------------------------------------------------------------------------

_GetVXDEntry@4 proc near

  push ebp
  mov  ebp, esp

  push es
  push edi

  mov  eax, 0
  mov  es, ax
  mov  eax, 0x00001684
  mov  ebx, [ebp+10]
  mov  edi, 0
  int  0x2F

  mov  eax, edi

  pop  edi
  pop  es

  pop  ebp
  ret

_GetVXDEntry@4 endp

;--------------------------------------------------------------------------

VXD ends

;--------------------------------------------------------------------------

end

