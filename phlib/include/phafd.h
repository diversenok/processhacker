/*
 * Copyright (c) 2022 Winsider Seminars & Solutions, Inc.  All rights reserved.
 *
 * This file is part of System Informer.
 *
 * Authors:
 *
 *     diversenok   2025
 *
 */

#ifndef _PH_PHAFD_H
#define _PH_PHAFD_H

#include <ph.h>
#include <ntafd.h>

EXTERN_C_START

PHLIBAPI
BOOLEAN
NTAPI
PhIsAfdSocketName(
    _In_ PPH_STRING ObjectName
    );

PHLIBAPI
NTSTATUS
NTAPI
PhIsAfdSocketHandle(
    _In_ HANDLE Handle
    );

NTSTATUS
NTAPI
PhAfdDeviceIoControl(
    _In_ HANDLE SocketHandle,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_(InBufferSize) PVOID InBuffer,
    _In_ ULONG InBufferSize,
    _Out_writes_bytes_to_opt_(OutputBufferSize, *BytesReturned) PVOID OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_opt_ PULONG BytesReturned,
    _Inout_opt_ LPOVERLAPPED Overlapped
    );

PHLIBAPI
NTSTATUS
NTAPI
PhAfdQuerySharedInfo(
    _In_ HANDLE SocketHandle,
    _Out_ PSOCK_SHARED_INFO SharedInfo
    );

PHLIBAPI
NTSTATUS
NTAPI
PhAfdQuerySimpleInfo(
    _In_ HANDLE SocketHandle,
    _In_ ULONG InformationType,
    _Out_ PAFD_INFORMATION Information
    );

PHLIBAPI
NTSTATUS
NTAPI
PhAfdQueryAddress(
    _In_ HANDLE SocketHandle,
    _In_ BOOLEAN Remote,
    _Out_ PSOCKADDR_STORAGE Address
    );

NTSTATUS
NTAPI
PhAfdQueryFormatAddress(
    _In_ HANDLE SocketHandle,
    _In_ BOOLEAN Remote,
    _Out_ PPH_STRING *AddressString
    );

_Maybenull_
PPH_STRING
NTAPI
PhAfdFormatSocketName(
    _In_ HANDLE SocketHandle
    );

EXTERN_C_END

#endif _PH_PHAFD_H
