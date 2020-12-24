/*++

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    shv.h

Abstract:

    This header defines the structures and functions of the Simple Hyper Visor.

Author:

    Alex Ionescu (@aionescu) 14-Mar-2016 - Initial version

Environment:

    Kernel mode only.

--*/

#pragma once
#pragma warning(disable:4201)
#pragma warning(disable:4214)

#include <basetsd.h>
#define _INC_MALLOC
#include <ntifs.h>
#include <intrin.h>
#include "shv_x.h"


#define DECLSPEC_ALIGN(x)   __declspec(align(x))
#define DECLSPEC_NORETURN   __declspec(noreturn)
#define FORCEINLINE         __forceinline

typedef struct _SHV_VP_STATE
{
    PCONTEXT VpRegs;
    uintptr_t GuestRip;
    uintptr_t GuestRsp;
    uintptr_t GuestEFlags;
    UINT16 ExitReason;
    UINT8 ExitVm;
} SHV_VP_STATE, *PSHV_VP_STATE;

typedef struct _SHV_CALLBACK_CONTEXT
{
    UINT64 Cr3;
    volatile long InitCount;
    INT32 FailedCpu;
    INT32 FailureStatus;
} SHV_CALLBACK_CONTEXT, *PSHV_CALLBACK_CONTEXT;


NTSTATUS
ShvVpLoadCallback(
    _In_ PSHV_CALLBACK_CONTEXT Context);

NTSTATUS
ShvVpUnloadCallback(
    _In_ PSHV_CALLBACK_CONTEXT Context);


VOID
ShvVmxEntry (
    VOID
    );

INT32
ShvVmxLaunchOnVp (
    _In_ PSHV_VP_DATA VpData);

VOID
ShvUtilConvertGdtEntry (
    _In_ VOID* GdtBase,
    _In_ UINT16 Offset,
    _Out_ PVMX_GDTENTRY64 VmxGdtEntry
    );

UINT32
ShvUtilAdjustMsr (
    _In_ LARGE_INTEGER ControlValue,
    _In_ UINT32 DesiredValue
    );

INT32
vmx_launch (
    VOID
    );

UINT8
ShvVmxProbe (
    VOID
    );

VOID
ShvVmxEptInitialize (
    _In_ PSHV_VP_DATA VpData
    );


//
// OS Layer
//
DECLSPEC_NORETURN
VOID
__cdecl
ShvOsRestoreContext (
    _In_ PCONTEXT ContextRecord
    );


VOID
ShvOsUnprepareProcessor (
    _In_ PSHV_VP_DATA VpData
    );



extern PSHV_VP_DATA* ShvGlobalData;

