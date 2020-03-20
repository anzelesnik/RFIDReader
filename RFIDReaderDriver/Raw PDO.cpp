#include "Nt.hpp"
#include "Configuration.hpp"
#include "Public.hpp"

#define NTSTRSAFE_LIB

#include <ntstrsafe.h>
#include <initguid.h>
#include <devguid.h>

#include <cstddef>
#include <cstdint>

void rawPdoDeviceControl(WDFQUEUE queue, WDFREQUEST request, std::size_t outputBufferLength,
	std::size_t inputBufferLength, std::uint32_t ioControlCode) {
	const auto device = WdfIoQueueGetDevice(queue);

	// If a correct control code is passed in, requeue the request into the keyboard request queue
	if (ioControlCode == Configuration::IoctlRequestReaderData) {
		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] IoctlRequestReaderData received\n");

		WDF_REQUEST_FORWARD_OPTIONS forwardOptions {};
		WDF_REQUEST_FORWARD_OPTIONS_INIT(&forwardOptions);
		const auto keyboardQueue = FilterGetRawPdoDeviceExt(device)->keyboardRequestQueue;
		const auto status = WdfRequestForwardToParentDeviceIoQueue(request, keyboardQueue, &forwardOptions);
		if (!NT_SUCCESS(status))
			WdfRequestComplete(request, status);
		
		return;
	}

	WdfRequestComplete(request, STATUS_NOT_IMPLEMENTED);
}

NTSTATUS initializeDeviceRawPdo(WDFDEVICE device, ULONG deviceIndex) {
	PWDFDEVICE_INIT deviceInit          {};
	UNICODE_STRING deviceId             {};
	WDFDEVICE pdoDevice                 {};
	WDF_IO_QUEUE_CONFIG ioQueueConfig   {};
	WDFQUEUE queue                      {};
	WDF_OBJECT_ATTRIBUTES pdoAttributes {};

	DECLARE_UNICODE_STRING_SIZE(deviceIndexString, 4);
	RtlInitUnicodeString(&deviceId, Configuration::filterDeviceId);

	// This lambda cleans up and returns if an error was encountered
	const auto exitError = [&](NTSTATUS status) {
		RtlFreeUnicodeString(&deviceId);
		
		if (deviceInit)
			WdfDeviceInitFree(deviceInit);

		if (pdoDevice)
			WdfObjectDelete(pdoDevice);

		if (queue)
			WdfObjectDelete(queue);

		return status;
	};

	// Allocate a new WDFDEVICE_INIT structure associated with the primary device  
	deviceInit = WdfPdoInitAllocate(device);
	if (!deviceInit)
		return exitError(STATUS_INSUFFICIENT_RESOURCES);

	// Set the new device as a raw PDO
	auto status = WdfPdoInitAssignRawDevice(deviceInit, &GUID_DEVCLASS_KEYBOARD);
	if (!NT_SUCCESS(status))
		return exitError(status);

	// Set a device ID required to create an interface later on
	status = WdfPdoInitAssignDeviceID(deviceInit, &deviceId);
	if (!NT_SUCCESS(status))
		return exitError(status);

	// Set an instance ID to prevent possible issues when connecting multiple same physical devices
	status = RtlUnicodeStringPrintf(&deviceIndexString, L"%02d", deviceIndex);
	if (!NT_SUCCESS(status))
		return exitError(status);

	status = WdfPdoInitAssignInstanceID(deviceInit, &deviceIndexString);
	if (!NT_SUCCESS(status))
		return exitError(status);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, RawPdoDeviceExtension);

	// We need to forward the request to the parent device so it can handle them in the callback
	WdfPdoInitAllowForwardingRequestToParent(deviceInit);

	// Create the raw PDO
	status = WdfDeviceCreate(const_cast<PWDFDEVICE_INIT*>(&deviceInit), &pdoAttributes, &pdoDevice);
	if (!NT_SUCCESS(status))
		return exitError(status);

	// Set a device control queue
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchSequential);
	ioQueueConfig.EvtIoDeviceControl = reinterpret_cast<decltype(ioQueueConfig.EvtIoDeviceControl)>(rawPdoDeviceControl);
	status = WdfIoQueueCreate(pdoDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
	if (!NT_SUCCESS(status))
		return exitError(status);

	FilterGetRawPdoDeviceExt(pdoDevice)->keyboardRequestQueue = FilterGetDeviceExt(device)->keyboardRequestQueue;

	// Create a device interface so we can send requests to the device from user mode
	status = WdfDeviceCreateDeviceInterface(pdoDevice, &Configuration::filterDeviceInterface, nullptr);
	if (!NT_SUCCESS(status))
		return exitError(status);

	// Finally add the PDO to the primary device
	status = WdfFdoAddStaticChild(device, pdoDevice);
	if (!NT_SUCCESS(status))
		return exitError(status);

	return STATUS_SUCCESS;
}