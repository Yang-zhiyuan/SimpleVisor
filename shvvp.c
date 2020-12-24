/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    shvvp.c

Abstract:

    This module implements Virtual Processor (VP) management for the Simple Hyper Visor.

Author:

    Alex Ionescu (@aionescu) 16-Mar-2016 - Initial version

Environment:

    Kernel mode only, IRQL DISPATCH_LEVEL.

--*/


#include "shv.h"
#include "ddk.h"


UINT8
ShvIsOurHypervisorPresent (
    VOID
    )
{
    //ULONGLONG cr4 = __readcr4();
    //// VMXE位 如果已经安装了VT, 则这个位会被置1
    //if (cr4 & (1 << 13))
    //{
    //    return TRUE;
    //}

    //return FALSE;

	
	
    INT32 cpuInfo[4];

    //
    // Check if ECX[31h] ("Hypervisor Present Bit") is set in CPUID 1h
    //
    __cpuid(cpuInfo, 1);
    if (cpuInfo[2] & HYPERV_HYPERVISOR_PRESENT_BIT)
    {
        //
        // Next, check if this is a compatible Hypervisor, and if it has the
        // SimpleVisor signature
        //
        __cpuid(cpuInfo, HYPERV_CPUID_INTERFACE);
        if (cpuInfo[0] == ' vhS')
        {
            DBG_PRINT("use cpuid, it's us");
            //
            // It's us!
            //
            return TRUE;
        }
    }

    //
    // No Hypervisor, or someone else's
    //
    return FALSE;
}

VOID
ShvCaptureSpecialRegisters (
    _In_ PSHV_SPECIAL_REGISTERS SpecialRegisters
    )
{
    //
    // Use compiler intrinsics to get the data we need
    //
    SpecialRegisters->Cr0 = __readcr0();
    SpecialRegisters->Cr3 = __readcr3();
    SpecialRegisters->Cr4 = __readcr4();
    SpecialRegisters->DebugControl = __readmsr(MSR_DEBUG_CTL);
    SpecialRegisters->MsrGsBase = __readmsr(MSR_GS_BASE);
    SpecialRegisters->KernelDr7 = __readdr(7);
    _sgdt(&SpecialRegisters->Gdtr.Limit);
    __sidt(&SpecialRegisters->Idtr.Limit);
    
    //
    // Use OS-specific functions to get these two
    //
    _str(&SpecialRegisters->Tr);
    _sldt(&SpecialRegisters->Ldtr);
}

NTSTATUS asm_vmx_launch(_In_ INT32 (*ShvVmxLaunchOnVp)(_In_ PSHV_VP_DATA),  _In_ PSHV_VP_DATA, _Inout_ PCONTEXT);

INT32
ShvVpInitialize (
    _In_ PSHV_VP_DATA Data
    )
{
    INT32 status = 0;

    
	// todo 保存特殊寄存器
    // Read the special control registers for this processor
    // Note: KeSaveStateForHibernate(&Data->HostState) can be used as a Windows
    // specific undocumented function that can also get this data.
    //
    ShvCaptureSpecialRegisters(&Data->SpecialRegisters);

	// 保存通用寄存器
    RtlCaptureContext(&Data->ContextFrame);
    
	// guest启动还原点在这个汇编函数中
	// 函数内设置了guest的rsp和rip
    status = asm_vmx_launch(ShvVmxLaunchOnVp, Data, &Data->ContextFrame);
	
    //
    // If we got here, the hypervisor is running :-)
    //
    return status;
}

NTSTATUS
ShvVpUnloadCallback (
    _In_ PSHV_CALLBACK_CONTEXT Context
    )
{
    INT32 cpuInfo[4];
    PSHV_VP_DATA vpData;
    UNREFERENCED_PARAMETER(Context);

    //
    // Send the magic shutdown instruction sequence. It will return in EAX:EBX
    // the VP data for the current CPU, which we must free.
    //
    cpuInfo[0] = cpuInfo[1] = 0;
    __cpuidex(cpuInfo, 0x41414141, 0x42424242);

    //
    // If SimpleVisor is disabled for some reason, CPUID will return the values
    // of the highest valid CPUID. We use a magic value to make sure we really
    // are loaded and returned something valid.
    //
    if (cpuInfo[2] == 0x43434343)
    {
        __analysis_assume((cpuInfo[0] != 0) && (cpuInfo[1] != 0));
        vpData = (PSHV_VP_DATA)((UINT64)cpuInfo[0] << 32 | (UINT32)cpuInfo[1]);
        MmFreeContiguousMemory(vpData);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
ShvVpLoadCallback (
    _In_ PSHV_CALLBACK_CONTEXT Context
    )
{
	// 每个cpu都执行的回调, 准备侵染cpu
    PSHV_VP_DATA vpData = NULL;
    INT32 status = 0;

	// todo 检查cpu是否支持vt
    //
    // Detect if the hardware appears to support VMX root mode to start.
    // No attempts are made to enable this if it is lacking or disabled.
    //
    if (ShvVmxProbe())
    {
        // todo 为当前cpu分配内存
        PHYSICAL_ADDRESS lowest, highest;
        lowest.QuadPart = 0;
        highest.QuadPart = lowest.QuadPart - 1;

    	// 申请读写属性内存
        vpData = MmAllocateContiguousNodeMemory(sizeof(*vpData), lowest, highest,
            lowest,
            PAGE_READWRITE,
            KeGetCurrentNodeNumber());
    	
        if (vpData)
        {
            __stosq((UINT64*)vpData, 0, sizeof(*vpData) / sizeof(UINT64));
        	
        	
            vpData->SystemDirectoryTableBase = Context->Cr3;

        	// 1.保存特殊和通用寄存器
            // 2.设置guest的rsp和rip
            // 3.填充MTRR, EPT, VMXON, VMCS区域
            // 4.启动虚拟机 vmlaunch
            status = ShvVpInitialize(vpData);
            if (status == SHV_STATUS_SUCCESS)
            {
                // 验证vt是否安装成功, 使用cpuid指令测试
                if (ShvIsOurHypervisorPresent())
                {
                    _InterlockedIncrement((volatile long*)&Context->InitCount);
                    return SHV_STATUS_SUCCESS;
                }
                else { status = SHV_STATUS_NOT_PRESENT; }
            }
        }
        else { status = SHV_STATUS_NO_RESOURCES; }
    }
    else { status = SHV_STATUS_NOT_AVAILABLE; }


	
    if (vpData != NULL)
    {
        MmFreeContiguousMemory(vpData);
    }
    Context->FailedCpu = (INT32)KeGetCurrentProcessorNumberEx(NULL);
    Context->FailureStatus = status;
    return status;
}
