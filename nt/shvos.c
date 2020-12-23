/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Module Name:

    shvos.c

Abstract:

    This module implements the OS-facing Windows stubs for SimpleVisor.

Author:

    Alex Ionescu (@aionescu) 29-Aug-2016 - Initial version

Environment:

    Kernel mode only.

--*/

#include <intrin.h>
#include <ntifs.h>
#include <stdarg.h>
#include "../ddk.h"
#include "../shv.h"
#include "..\shv_x.h"
#pragma warning(disable:4221)
#pragma warning(disable:4204)


DECLSPEC_NORETURN
VOID
__cdecl
ShvOsRestoreContext2 (
    _In_ PCONTEXT ContextRecord,
    _In_opt_ struct _EXCEPTION_RECORD * ExceptionRecord
    );

VOID
ShvVmxCleanup (
    _In_ UINT16 Data,
    _In_ UINT16 Teb
    );

typedef struct _SHV_DPC_CONTEXT
{
    PSHV_CPU_CALLBACK Routine;
    struct _SHV_CALLBACK_CONTEXT* Context;
} SHV_DPC_CONTEXT, *PSHV_DPC_CONTEXT;

#define KGDT64_R3_DATA      0x28
#define KGDT64_R3_CMTEB     0x50


NTSTATUS
FORCEINLINE
ShvOsErrorToError (
    INT32 Error
    )
{
    //
    // Convert the possible SimpleVisor errors into NT Hyper-V Errors
    //
    if (Error == SHV_STATUS_NOT_AVAILABLE)
    {
        DBG_PRINT("STATUS_HV_FEATURE_UNAVAILABLE");
        return STATUS_HV_FEATURE_UNAVAILABLE;
    }
    if (Error == SHV_STATUS_NO_RESOURCES)
    {
        DBG_PRINT("STATUS_HV_NO_RESOURCES");
        return STATUS_HV_NO_RESOURCES;
    }
    if (Error == SHV_STATUS_NOT_PRESENT)
    {
        DBG_PRINT("STATUS_HV_NOT_PRESENT");
        return STATUS_HV_NOT_PRESENT;
    }
    if (Error == SHV_STATUS_SUCCESS)
    {
        return STATUS_SUCCESS;
    }

    DBG_PRINT("STATUS_UNSUCCESSFUL");
    //
    // Unknown/unexpected error
    //
    return STATUS_UNSUCCESSFUL;
}

VOID
ShvOsDpcRoutine (
    _In_ struct _KDPC *Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PSHV_DPC_CONTEXT dpcContext = DeferredContext;
    UNREFERENCED_PARAMETER(Dpc);

    __analysis_assume(DeferredContext != NULL);
    __analysis_assume(SystemArgument1 != NULL);
    __analysis_assume(SystemArgument2 != NULL);

    //
    // Execute the internal callback function
    //
    dpcContext->Routine(dpcContext->Context);

    //
    // During unload SimpleVisor uses the RtlRestoreContext function which will
    // unfortunately use the "iretq" opcode in order to restore execution back.
    // This causes the processor to remove the RPL bits off the segments. As
    // the x64 kernel does not expect kernel-mode code to change the value of
    // any segments, this results in the DS and ES segments being stuck 0x20,
    // and the FS segment being stuck at 0x50, until the next context switch.
    //
    // If the DPC happened to have interrupted either the idle thread or system
    // thread, that's perfectly fine (albeit unusual). If the DPC interrupted a
    // 64-bit long-mode thread, that's also fine. However if the DPC interrupts
    // a thread in compatibility-mode, running as part of WoW64, it will hit a
    // GPF instantaneously and crash.
    //
    // Thus, set the segments to their correct value, one more time, as a fix.
    //
	// todo 是由什么东西导致的必须要修复?
    ShvVmxCleanup(KGDT64_R3_DATA | RPL_MASK, KGDT64_R3_CMTEB | RPL_MASK);

    //
    // Wait for all DPCs to synchronize at this point
    //
    KeSignalCallDpcSynchronize(SystemArgument2);

    //
    // Mark the DPC as being complete
    //
    KeSignalCallDpcDone(SystemArgument1);
}

VOID
ShvOsUnprepareProcessor (
    _In_ PSHV_VP_DATA VpData
    )
{
    //
    // When running in VMX root mode, the processor will set limits of the
    // GDT and IDT to 0xFFFF (notice that there are no Host VMCS fields to
    // set these values). This causes problems with PatchGuard, which will
    // believe that the GDTR and IDTR have been modified by malware, and
    // eventually crash the system. Since we know what the original state
    // of the GDTR and IDTR was, simply restore it now.
    //
    __lgdt(&VpData->SpecialRegisters.Gdtr.Limit);
    __lidt(&VpData->SpecialRegisters.Idtr.Limit);
}

VOID
ShvOsFreeContiguousAlignedMemory (
    _In_ PVOID BaseAddress,
    _In_ size_t Size
    )
{
    //
    // Free the memory
    //
    MmFreeContiguousMemory(BaseAddress);
}

PVOID
ShvOsAllocateContigousAlignedMemory (
    _In_ SIZE_T Size
    )
{
    PHYSICAL_ADDRESS lowest, highest;

    //
    // The entire address range is OK for this allocation
    //
    lowest.QuadPart = 0;
    highest.QuadPart = lowest.QuadPart - 1;

    //
    // Allocate a contiguous chunk of RAM to back this allocation and make sure
    // that it is RW only, instead of RWX, by using the new Windows 8 API.
    //
    return MmAllocateContiguousNodeMemory(Size,
                                          lowest,
                                          highest,
                                          lowest,
                                          PAGE_READWRITE,
                                          KeGetCurrentNodeNumber());
}

ULONGLONG
ShvOsGetPhysicalAddress (
    _In_ PVOID BaseAddress
    )
{
    //
    // Let the memory manager convert it
    //
    return MmGetPhysicalAddress(BaseAddress).QuadPart;
}

VOID
ShvOsRunCallbackOnProcessors (
    _In_ PSHV_CPU_CALLBACK Routine,
    _In_opt_ PVOID Context
    )
{
    SHV_DPC_CONTEXT dpcContext;

    //
    // Wrap the internal routine and context under a Windows DPC
    //
    dpcContext.Routine = Routine;
    dpcContext.Context = Context;
    KeGenericCallDpc(ShvOsDpcRoutine, &dpcContext);
}



NTSTATUS
UtilForEachProcessor(NTSTATUS(*callback_routine)(void*), void* context) {
    PAGED_CODE()

        const auto number_of_processors =
        KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    for (ULONG processor_index = 0; processor_index < number_of_processors;
        processor_index++) {
        PROCESSOR_NUMBER processor_number = {0};
        auto status =
            KeGetProcessorNumberFromIndex(processor_index, &processor_number);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        // Switch the current processor
        GROUP_AFFINITY affinity = {0};
        affinity.Group = processor_number.Group;
        affinity.Mask = 1ull << processor_number.Number;
        GROUP_AFFINITY previous_affinity = {0};
        KeSetSystemGroupAffinityThread(&affinity, &previous_affinity);

        // Execute callback
        status = callback_routine(context);

        KeRevertToUserGroupAffinityThread(&previous_affinity);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    return STATUS_SUCCESS;
}



// 还原寄存器
VOID
ShvOsRestoreContext(
    _In_ PCONTEXT ContextRecord
    )
{
    ShvOsRestoreContext2(ContextRecord, NULL);
}

// 获取当前寄存器
VOID
ShvOsCaptureContext (
    _In_ PCONTEXT ContextRecord
    )
{
	// todo 如果是无优化编译, 这里的rip会导致vm卡死
    
    //
    // Windows provides a nice OS function to do this
    //
    RtlCaptureContext(ContextRecord);
}



VOID
DriverUnload (
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
	

    //
    // Attempt to exit VMX root mode on all logical processors. This will
    // broadcast an interrupt which will execute the callback routine in
    // parallel on the LPs.
    //
    // Note that if SHV is not loaded on any of the LPs, this routine will not
    // perform any work, but will not fail in any way.
    //
    ShvOsRunCallbackOnProcessors(ShvVpUnloadCallback, NULL);

    //
    // Indicate unload
    //
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "The SHV has been uninstalled.\n");
}

EXTERN_C
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;

    ////
    //// Make the driver (and SHV itself) unloadable
    ////
    DriverObject->DriverUnload = DriverUnload;

	
    // todo 通过KeGenericCallDpc设置每个cpu指令
    SHV_CALLBACK_CONTEXT callbackContext;
    //
    // Attempt to enter VMX root mode on all logical processors. This will
    // broadcast a DPC interrupt which will execute the callback routine in
    // parallel on the LPs. Send the callback routine the physical address of
    // the PML4 of the system process, which is what this driver entrypoint
    // should be executing in.
    //
    callbackContext.Cr3 = __readcr3();
    callbackContext.FailureStatus = SHV_STATUS_SUCCESS;
    callbackContext.FailedCpu = -1;
    callbackContext.InitCount = 0;
    UtilForEachProcessor(ShvVpLoadCallback, &callbackContext);

	// todo 查询初始化的cpu数量与机器的cpu数量是否相等
    //
    // Check if all LPs are now hypervised. Return the failure code of at least
    // one of them. 
    //
    // Note that each VP is responsible for freeing its VP data on failure.
    //
    if (callbackContext.InitCount != (INT32)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS))
    {
        DBG_PRINT("The SHV failed to initialize (0x%lX) Failed CPU: %d\n", 
            callbackContext.FailureStatus, callbackContext.FailedCpu);
    	
        status = callbackContext.FailureStatus;
    }
    else
    {
        DBG_PRINT("The SHV has been installed.\n");
        status = SHV_STATUS_SUCCESS;
    }
	
    status = ShvOsErrorToError(status);

    return status;
}