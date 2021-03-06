
include ksamd64.inc

    LEAF_ENTRY _str, _TEXT$00
        str word ptr [rcx]          ; Store TR value
        ret                         ; Return
    LEAF_END _str, _TEXT$00

    LEAF_ENTRY _sldt, _TEXT$00
        sldt word ptr [rcx]         ; Store LDTR value
        ret                         ; Return
    LEAF_END _sldt, _TEXT$00
    
    LEAF_ENTRY OsRestoreContext2, _TEXT$00
        movaps  xmm0, CxXmm0[rcx]   ; CxXXXX这种应该是一个汇编宏, 宏内部计算了CONTEXT的偏移
        movaps  xmm1, CxXmm1[rcx]   ;
        movaps  xmm2, CxXmm2[rcx]   ;
        movaps  xmm3, CxXmm3[rcx]   ;
        movaps  xmm4, CxXmm4[rcx]   ;
        movaps  xmm5, CxXmm5[rcx]   ;
        movaps  xmm6, CxXmm6[rcx]   ; Restore all XMM registers
        movaps  xmm7, CxXmm7[rcx]   ;
        movaps  xmm8, CxXmm8[rcx]   ;
        movaps  xmm9, CxXmm9[rcx]   ;
        movaps  xmm10, CxXmm10[rcx] ;
        movaps  xmm11, CxXmm11[rcx] ;
        movaps  xmm12, CxXmm12[rcx] ;
        movaps  xmm13, CxXmm13[rcx] ;
        movaps  xmm14, CxXmm14[rcx] ;
        movaps  xmm15, CxXmm15[rcx] ;
        ldmxcsr CxMxCsr[rcx]        ;

        mov     rax, CxRax[rcx]     ;
        mov     rdx, CxRdx[rcx]     ;
        mov     r8, CxR8[rcx]       ; Restore volatile registers
        mov     r9, CxR9[rcx]       ;
        mov     r10, CxR10[rcx]     ;
        mov     r11, CxR11[rcx]     ;

        mov     rbx, CxRbx[rcx]     ;
        mov     rsi, CxRsi[rcx]     ;
        mov     rdi, CxRdi[rcx]     ;
        mov     rbp, CxRbp[rcx]     ; Restore non volatile regsiters
        mov     r12, CxR12[rcx]     ;
        mov     r13, CxR13[rcx]     ;
        mov     r14, CxR14[rcx]     ;
        mov     r15, CxR15[rcx]     ;

        cli                         ; Disable interrupts
        push    CxEFlags[rcx]       ; Push RFLAGS on stack
        popfq                       ; Restore RFLAGS
        mov     rsp, CxRsp[rcx]     ; Restore old stack
        push    CxRip[rcx]          ; Push RIP on old stack
        mov     rcx, CxRcx[rcx]     ; Restore RCX since we spilled it
        ret                         ; Restore RIP
    LEAF_END OsRestoreContext2, _TEXT$00


    ; Saves all general purpose registers to the stack
    PUSHAQ MACRO
        push    rax
        push    rcx
        push    rdx
        push    rbx
        push    -1      ; dummy for rsp
        push    rbp
        push    rsi
        push    rdi
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
    ENDM

    ; Loads all general purpose registers from the stack
    POPAQ MACRO
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdi
        pop     rsi
        pop     rbp
        add     rsp, 8    ; dummy for rsp
        pop     rbx
        pop     rdx
        pop     rcx
        pop     rax
    ENDM

    
.code

    extern launch_vm:proc;

    ; vm entry point
    asm_vmx_launch PROC
    pushfq
    PUSHAQ
    
    mov CxRsp[r8], rsp      ; rsp
    mov rax, guest_run      
    mov CxRip[r8], rax      ; rip
    mov rax, rcx            ; launch_vm address

    sub rsp, 20h
    mov rcx, rdx            ; vpData
    call rax
    add rsp, 20h

    POPAQ
    popfq
    ret

guest_run:                  ; guest恢复执行点
    nop
    POPAQ
    popfq
    xor rax, rax
    ret

    asm_vmx_launch ENDP



    ; vmm entry point
    extern VmxEntryHandler:proc
    extern RtlCaptureContext:proc
    
    vmm_entry PROC
    ; 保存rcx寄存器到堆栈

    sub rsp, CONTEXT_FRAME_LENGTH   ; 开辟context空间
    
    push    rcx                     ; save the RCX register, which we spill below
    lea     rcx, [rsp+8h]           ; store the context in the stack, bias for
                                    ; the return address and the push we just did.
    call    RtlCaptureContext       ; save the current register state.
                                    ; note that this is a specially written function
                                    ; which has the following key characteristics:
                                    ;   1) it does not taint the value of RCX
                                    ;   2) it does not spill any registers, nor
                                    ;      expect home space to be allocated for it
    jmp     VmxEntryHandler      ; jump to the C code handler. we assume that it
                                    ; compiled with optimizations and does not use
                                    ; home space, which is true of release builds.
    ; 跳到VmxEntryHandler函数内部后从栈中恢复rcx寄存器
    vmm_entry ENDP

end