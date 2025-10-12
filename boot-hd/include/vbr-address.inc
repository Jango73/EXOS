%ifndef VBR_ADDRESS_INC
%define VBR_ADDRESS_INC

%define LINEAR_TO_SEGMENT(addr)        ((addr) >> 4)
%define LINEAR_TO_OFFSET(addr)         ((addr) & 0xF)
%define LINEAR_TO_STACK_SEGMENT(addr)  (((addr) - 0x10000) >> 4)
%define LINEAR_TO_STACK_OFFSET(addr)   0xFFFF

%endif
