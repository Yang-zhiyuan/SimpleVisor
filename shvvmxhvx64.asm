;++
;
; Copyright (c) Alex Ionescu.  All rights reserved.
;
; Module:
;
;    shvvmxhvx64.asm
;
; Abstract:
;
;    This module implements the AMD64-specific SimpleVisor VMENTRY routine.
;
; Author:
;
;    Alex Ionescu (@aionescu) 16-Mar-2016 - Initial version
;
; Environment:
;
;    Kernel mode only.
;
;--

include ksamd64.inc

    .code

    extern ShvVmxEntryHandler:proc
    extern RtlCaptureContext:proc

    ShvVmxEntry PROC
    ; ����rcx�Ĵ�������ջ

    sub rsp, CONTEXT_FRAME_LENGTH   ; ����context�ռ�
    
    push    rcx                     ; save the RCX register, which we spill below
    lea     rcx, [rsp+8h]           ; store the context in the stack, bias for
                                    ; the return address and the push we just did.
    call    RtlCaptureContext       ; save the current register state.
                                    ; note that this is a specially written function
                                    ; which has the following key characteristics:
                                    ;   1) it does not taint the value of RCX
                                    ;   2) it does not spill any registers, nor
                                    ;      expect home space to be allocated for it
    jmp     ShvVmxEntryHandler      ; jump to the C code handler. we assume that it
                                    ; compiled with optimizations and does not use
                                    ; home space, which is true of release builds.
    ; ����ShvVmxEntryHandler�����ڲ����ջ�лָ�rcx�Ĵ���
    ShvVmxEntry ENDP

    end
