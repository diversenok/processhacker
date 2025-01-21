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

#include <phafd.h>
#include <ws2ipdef.h>
#include <ws2bth.h>
#include <hvsocket.h>

 /**
   * \brief Determines if an object name represents an AFD socket handle.
   *
   * \param[in] ObjectName A native object name.
   *
   * \return Whether the name matches an AFD socket name format.
   */
BOOLEAN
NTAPI
PhIsAfdSocketName(
    _In_ PPH_STRING ObjectName
    )
{
    return PhStartsWithString2(ObjectName, AFD_DEVICE_NAME, TRUE) &&
        (ObjectName->Length == sizeof(AFD_DEVICE_NAME) - sizeof(UNICODE_NULL) ||
         ObjectName->Buffer[ObjectName->Length / sizeof(WCHAR)] == OBJ_NAME_PATH_SEPARATOR);
}

/**
  * \brief Determines if a file handle is an AFD socket handle.
  *
  * \param[in] Handle A file handle.
  *
  * \return A successful status if the handle is an AFD socket or an errant status otherwise.
  */
NTSTATUS
NTAPI
PhIsAfdSocketHandle(
    _In_ HANDLE Handle
    )
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatusBlock;

    union {
        FILE_VOLUME_NAME_INFORMATION VolumeName;
        UCHAR Raw[sizeof(FILE_VOLUME_NAME_INFORMATION) + sizeof(AFD_DEVICE_NAME) - sizeof(UNICODE_NULL)];
    } Buffer;

    // Query the backing device name
    status = NtQueryInformationFile(
        Handle,
        &ioStatusBlock,
        &Buffer,
        sizeof(Buffer),
        FileVolumeNameInformation
        );

    // If the name does not fit into the buffer, it's not AFD
    if (status == STATUS_BUFFER_OVERFLOW)
        return STATUS_NOT_SAME_DEVICE;

    if (!NT_SUCCESS(status))
        return status;

    static UNICODE_STRING afdDeviceName = RTL_CONSTANT_STRING(AFD_DEVICE_NAME);
    UNICODE_STRING volumeName;

    volumeName.Buffer = Buffer.VolumeName.DeviceName;
    volumeName.Length = (USHORT)Buffer.VolumeName.DeviceNameLength;
    volumeName.MaximumLength = (USHORT)Buffer.VolumeName.DeviceNameLength;

    // Compare the file's device name to AFD
    return RtlEqualUnicodeString(&volumeName, &afdDeviceName, TRUE) ? STATUS_SUCCESS : STATUS_NOT_SAME_DEVICE;
}

 /**
  * \brief Issues an IOCTL on an AFD handle.
  *
  * \param[in] SocketHandle An AFD socket handle.
  * \param[in] IoControlCode I/O control code
  * \param[in] InBuffer Input buffer.
  * \param[in] InBufferSize Input buffer size.
  * \param[out] OutputBuffer Output Buffer.
  * \param[in] OutputBufferSize Output buffer size.
  * \param[out] BytesReturned Optionally set to the number of bytes returned.
  * \param[in,out] Overlapped Optional overlapped structure.
  *
  * \return Successful or errant status.
  */
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
    )
{
    NTSTATUS status;
    HANDLE hEvent;
    IO_STATUS_BLOCK ioStatusBlock;

    if (BytesReturned)
    {
        *BytesReturned = 0;
    }

    if (Overlapped)
    {
        hEvent = Overlapped->hEvent;
        Overlapped->Internal = STATUS_PENDING;
    }
    else
    {
        // We cannot wait on the file handle because it might not grant SYNCHRONIZE access.
        // Always use an event instead.

        status = NtCreateEvent(
            &hEvent,
            EVENT_ALL_ACCESS,
            NULL,
            SynchronizationEvent,
            FALSE
            );

        if (!NT_SUCCESS(status))
            return status;
    }

    status = NtDeviceIoControlFile(
        SocketHandle,
        hEvent,
        NULL,
        Overlapped,
        Overlapped ? (PIO_STATUS_BLOCK)Overlapped : &ioStatusBlock,
        IoControlCode,
        InBuffer,
        InBufferSize,
        OutputBuffer,
        OutputBufferSize
    );

    if (Overlapped)
    {
        if (NT_INFORMATION(status) && BytesReturned)
        {
            *BytesReturned = (ULONG)Overlapped->InternalHigh;
        }
    }
    else
    {
        if (status == STATUS_PENDING)
        {
            NtWaitForSingleObject(hEvent, FALSE, NULL);
            status = ioStatusBlock.Status;
        }

        NtClose(hEvent);

        if (BytesReturned)
        {
            *BytesReturned = (ULONG)ioStatusBlock.Information;
        }
    }

    return status;
}

/**
  * \brief Retrieves shared information for an AFD socket.
  *
  * \param[in] SocketHandle An AFD socket handle.
  * \param[out] SharedInfo A buffer with the shared socket information.
  *
  * \return Successful or errant status.
  */
NTSTATUS
NTAPI
PhAfdQuerySharedInfo(
    _In_ HANDLE SocketHandle,
    _Out_ PSOCK_SHARED_INFO SharedInfo
    )
{
    NTSTATUS status;
    ULONG returnedSize;

    status = PhAfdDeviceIoControl(
        SocketHandle,
        IOCTL_AFD_GET_CONTEXT,
        NULL,
        0,
        SharedInfo,
        sizeof(SOCK_SHARED_INFO),
        &returnedSize,
        NULL
        );

    if (status == STATUS_BUFFER_OVERFLOW)
        return STATUS_SUCCESS;

    if (!NT_SUCCESS(status))
        return status;

    // Shared information is provided on the Win32 level, so do a sanity check on the returned size
    return returnedSize < sizeof(SOCK_SHARED_INFO) ? STATUS_NOT_FOUND : status;
}

/**
  * \brief Retrieves simple information for an AFD socket.
  *
  * \param[in] SocketHandle An AFD socket handle.
  * \param[in] InformationType The type of information to query.
  * \param[out] Information Output buffer.
  *
  * \return Successful or errant status.
  */
NTSTATUS
NTAPI
PhAfdQuerySimpleInfo(
    _In_ HANDLE SocketHandle,
    _In_ ULONG InformationType,
    _Out_ PAFD_INFORMATION Information
    )
{
    Information->InformationType = InformationType;

    return PhAfdDeviceIoControl(
        SocketHandle,
        IOCTL_AFD_GET_INFORMATION,
        &Information,
        sizeof(AFD_INFORMATION),
        &Information,
        sizeof(AFD_INFORMATION),
        NULL,
        NULL
        );
}

/**
  * \brief Determines if we know how to handle a specific address family.
  *
  * \param[in] AddressFamily A socket address family value.
  *
  * \return Whether the address family is supported.
  */
BOOLEAN
NTAPI
PhpAfdIsSupportedAddressFamily(
    _In_ LONG AddressFamily
)
{
    switch (AddressFamily)
    {
        case AF_INET:
        case AF_INET6:
        case AF_BTH:
        case AF_HYPERV:
            return TRUE;
        default:
            return FALSE;
    }
}

/**
  * \brief Retrieves an address associated with an AFD socket.
  * 
  * \param[in] SocketHandle An AFD socket handle.
  * \param[in] Remote Whether the function should return a remote or a local address.
  * \param[out] Address Output buffer.
  *
  * \return Successful or errant status.
  */
NTSTATUS
NTAPI
PhAfdQueryAddress(
    _In_ HANDLE SocketHandle,
    _In_ BOOLEAN Remote,
    _Out_ PSOCKADDR_STORAGE Address
    )
{
    NTSTATUS status;
    AFD_ADDRESS buffer;

    // Retrieve the address
    status = PhAfdDeviceIoControl(
        SocketHandle,
        Remote ? IOCTL_AFD_GET_REMOTE_ADDRESS : IOCTL_AFD_GET_ADDRESS,
        NULL,
        0,
        &buffer,
        sizeof(buffer),
        NULL,
        NULL
        );

    if (!NT_SUCCESS(status))
        return status;

    // Most sockets are TLI; their addresses don't need conversion.
    if (PhpAfdIsSupportedAddressFamily(buffer.TliAddress.ss_family))
    {
        *Address = buffer.TliAddress;
        return status;
    }

    // Some sockets (like Bluetooth) use TDI. Verify the header and extarct the socket address.
    if (buffer.TdiAddress.ActivityCount > 0 &&
        buffer.TdiAddress.Address.TAAddressCount >= 1 &&
        buffer.TdiAddress.Address.Address[0].AddressLength <= sizeof(buffer) - RTL_SIZEOF_THROUGH_FIELD(TDI_ADDRESS_INFO, Address.Address[0].AddressType) &&
        PhpAfdIsSupportedAddressFamily(buffer.TdiAddress.Address.Address[0].AddressType))
    {
        RtlZeroMemory(Address, sizeof(SOCKADDR_STORAGE));

        // AddressLength covers the length after the AddressType field, while the socket address starts at the AddressType field.
        RtlCopyMemory(
            Address,
            &buffer.TdiAddressUnpacked.EmbeddedAddress,
            buffer.TdiAddress.Address.Address[0].AddressLength + RTL_FIELD_SIZE(TDI_ADDRESS_INFO, Address.Address[0].AddressType)
            );

        return status;
    }

    return STATUS_UNKNOWN_REVISION;
}

/**
  * \brief Formats a socket address as a strings.
  *
  * \param[in] Address The address buffer.
  * \param[out] AddressString The string representation of the address.
  *
  * \return Successful or errant status.
  */
NTSTATUS
NTAPI
PhAfdFormatAddress(
    _In_ PSOCKADDR_STORAGE Address,
    _Out_ PPH_STRING *AddressString
    )
{
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    WCHAR buffer[70];
    ULONG characters = RTL_NUMBER_OF(buffer);

    if (Address->ss_family == AF_INET)
    {
        PSOCKADDR_IN address = (PSOCKADDR_IN)Address;

        // Format an IPv4 address
        status = RtlIpv4AddressToStringExW(
            &address->sin_addr,
            address->sin_port,
            buffer,
            &characters
            );

        if (!NT_SUCCESS(status))
            return status;

        *AddressString = PhCreateStringEx(buffer, characters * sizeof(WCHAR) - sizeof(UNICODE_NULL));
    }
    else if (Address->ss_family == AF_INET6)
    {
        PSOCKADDR_IN6 address = (PSOCKADDR_IN6)Address;

        // Format an IPv6 address
        status = RtlIpv6AddressToStringExW(
            &address->sin6_addr,
            address->sin6_scope_id,
            address->sin6_port,
            buffer,
            &characters
            );

        if (!NT_SUCCESS(status))
            return status;

        *AddressString = PhCreateStringEx(buffer, characters * sizeof(WCHAR) - sizeof(UNICODE_NULL));
    }
    else if (Address->ss_family == AF_BTH)
    {
        PSOCKADDR_BTH address = (PSOCKADDR_BTH)Address;

        // Format a Bluetooth address
        *AddressString = PhFormatString(
            L"(%02X:%02X:%02X:%02X:%02X:%02X):%d",
            (UCHAR)(address->btAddr >> 40),
            (UCHAR)(address->btAddr >> 32),
            (UCHAR)(address->btAddr >> 24),
            (UCHAR)(address->btAddr >> 16),
            (UCHAR)(address->btAddr >> 8),
            (UCHAR)(address->btAddr),
            address->port
            );

        status = STATUS_SUCCESS;
    }
    else if (Address->ss_family == AF_HYPERV)
    {
        PSOCKADDR_HV address = (PSOCKADDR_HV)Address;

        // Format a Hyper-V address as {VmId}:{ServiceId}
        *AddressString = PhFormatString(
            L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}:{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            address->VmId.Data1,
            address->VmId.Data2,
            address->VmId.Data3,
            address->VmId.Data4[0],
            address->VmId.Data4[1],
            address->VmId.Data4[2],
            address->VmId.Data4[3],
            address->VmId.Data4[4],
            address->VmId.Data4[5],
            address->VmId.Data4[6],
            address->VmId.Data4[7],
            address->ServiceId.Data1,
            address->ServiceId.Data2,
            address->ServiceId.Data3,
            address->ServiceId.Data4[0],
            address->ServiceId.Data4[1],
            address->ServiceId.Data4[2],
            address->ServiceId.Data4[3],
            address->ServiceId.Data4[4],
            address->ServiceId.Data4[5],
            address->ServiceId.Data4[6],
            address->ServiceId.Data4[7]
            );

        status = STATUS_SUCCESS;
    }

    return status;
}

/**
  * \brief Queries an address associated with an AFD socket and prints it into a string.
  *
  * \param[in] SocketHandle An AFD socket handle.
  * \param[in] Remote Whether the function should return a remote or a local address.
  * \param[out] AddressString The string representation of the address.
  *
  * \return Successful or errant status.
  */
NTSTATUS
NTAPI
PhAfdQueryFormatAddress(
    _In_ HANDLE SocketHandle,
    _In_ BOOLEAN Remote,
    _Out_ PPH_STRING *AddressString
    )
{
    NTSTATUS status;
    SOCKADDR_STORAGE address;

    status = PhAfdQueryAddress(SocketHandle, Remote, &address);

    if (!NT_SUCCESS(status))
        return status;

    return PhAfdFormatAddress(&address, AddressString);
}

/**
  * \brief Looks up a human-readable name of a known protocol.
  *
  * \param[in] AddressFamily The address family of the protocol.
  * \param[in] Protocol The protocol value.
  *
  * \return A string with the protocol name.
  */
PCWSTR
NTAPI
PhAfdGetProtocolName(
    _In_ LONG AddressFamily,
    _In_ LONG Protocol
)
{
	switch (AddressFamily)
	{
		case AF_INET:
			switch (Protocol)
			{
                case IPPROTO_ICMP:
                    return L"ICMP";
				case IPPROTO_TCP:
					return L"TCP";
				case IPPROTO_UDP:
					return L"UDP";
                case IPPROTO_RDP:
                    return L"RDP";
                case IPPROTO_SCTP:
                    return L"SCTP";
                case IPPROTO_RESERVED_IPSEC:
                    return L"IPSec";
                case IPPROTO_RAW:
                    return L"RAW/IPv4";
			}
		case AF_INET6:
			switch (Protocol)
			{
                case IPPROTO_ICMPV6:
                    return L"ICMP6";
                case IPPROTO_TCP:
					return L"TCP6";
				case IPPROTO_UDP:
					return L"UDP6";
                case IPPROTO_RDP:
                    return L"RDP6";
                case IPPROTO_SCTP:
                    return L"SCTP6";
                case IPPROTO_RESERVED_IPSEC:
                    return L"IPSec6";
                case IPPROTO_RAW:
                    return L"RAW/IPv6";
            }
		case AF_BTH:
			switch (Protocol)
			{
				case BTHPROTO_RFCOMM:
					return L"RFCOMM [Bluetooth]";
				case BTHPROTO_L2CAP:
					return L"L2CAP [Bluetooth]";
			}
		case AF_HYPERV:
			switch (Protocol) 
			{
				case HV_PROTOCOL_RAW:
					return L"Hyper-V RAW";
			}
	}

	return L"unrecognized protocol";
}

/**
  * \brief Formats a human-readable name for an AFD socket.
  *
  * \param[in] SocketHandle An AFD socket handle.
  *
  * \return The best handle name representation available on success, or NULL on error.
  */
_Maybenull_
PPH_STRING
NTAPI
PhAfdFormatSocketName(
    _In_ HANDLE SocketHandle
    )
{
    PH_STRING_BUILDER stringBuilder;
    SOCK_SHARED_INFO sharedInfo;
    PPH_STRING addressString;
    BOOLEAN sharedInfoAvailable;
    BOOLEAN addressAvailable;

    // Query socket information
    sharedInfoAvailable = NT_SUCCESS(PhAfdQuerySharedInfo(SocketHandle, &sharedInfo));
    addressAvailable = NT_SUCCESS(PhAfdQueryFormatAddress(SocketHandle, FALSE, &addressString));

    if (!sharedInfoAvailable && !addressAvailable)
        return NULL;

    PhInitializeStringBuilder(&stringBuilder, 0x100);
    PhAppendStringBuilder2(&stringBuilder, L"AFD socket: ");

    if (sharedInfoAvailable)
    {
        // Socket state
        switch (sharedInfo.State)
        {
            case SocketStateInitializing:
                PhAppendStringBuilder2(&stringBuilder, L"initializing ");
                break;
            case SocketStateOpen:
                PhAppendStringBuilder2(&stringBuilder, L"open ");
                break;
            case SocketStateBound:
                PhAppendStringBuilder2(&stringBuilder, L"bound ");
                break;
            case SocketStateBoundSpecific:
                PhAppendStringBuilder2(&stringBuilder, L"bound (specific) ");
                break;
            case SocketStateConnected:
                PhAppendStringBuilder2(&stringBuilder, L"connected ");
                break;
            case SocketStateClosing:
                PhAppendStringBuilder2(&stringBuilder, L"closing ");
                break;
        }

        // Socket protocol
        PhAppendStringBuilder2(&stringBuilder, PhAfdGetProtocolName(sharedInfo.AddressFamily, sharedInfo.Protocol));
        PhAppendStringBuilder2(&stringBuilder, L" ");
    }

    if (addressAvailable)
    {
        PPH_STRING remoteAddressString;

        // Local address
        PhAppendStringBuilder2(&stringBuilder, L"on ");
        PhAppendStringBuilder(&stringBuilder, &addressString->sr);

        // Remote address
        if (NT_SUCCESS(PhAfdQueryFormatAddress(SocketHandle, TRUE, &remoteAddressString)))
        {
            PhAppendStringBuilder2(&stringBuilder, L" --> ");
            PhAppendStringBuilder(&stringBuilder, &remoteAddressString->sr);
            PhDereferenceObject(remoteAddressString);
        }

        PhDereferenceObject(addressString);
    }

    return PhFinalStringBuilderString(&stringBuilder);
}
