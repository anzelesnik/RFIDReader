#include "Nt.hpp"
#include"Configuration.hpp"
#include "Public.hpp"

#include <cstddef>
#include <cstdint>

void internalDeviceControl(WDFQUEUE queue, WDFREQUEST request, std::size_t outputBufferLength,
	std::size_t inputBufferLength, std::uint32_t ioControlCode) {
	const auto device = WdfIoQueueGetDevice(queue);
	const auto devExt = FilterGetDeviceExt(device);

	// This IOCTL is sent down the device stack by the keyboard class driver when a new physical device is connected
	if (ioControlCode == IOCTL_INTERNAL_KEYBOARD_CONNECT) {
		// Check if a hook was already registered for the current device
		if (devExt->upperConnectData.ClassService) {
			if (Configuration::debugPrint)
				DbgPrintEx(0, 0, "[RFID Reader] %s: Keyboard Class Service Callback already hooked for device\n", __FUNCTION__);

			WdfRequestComplete(request, STATUS_SHARING_VIOLATION);

			return;
		}

		// Retrieve the input buffer which contains the CONNECT_DATA structure
		PCONNECT_DATA connectData {};
		std::size_t bufferLength  {};
		const auto status = WdfRequestRetrieveInputBuffer(request, sizeof(CONNECT_DATA),
			reinterpret_cast<PVOID*>(&connectData), &bufferLength);
		if (!NT_SUCCESS(status)) {
			if (Configuration::debugPrint)
				DbgPrintEx(0, 0, "[RFID Reader] %s: Error occured while retrieving input buffer: 0x%X\n", __FUNCTION__, status);

			WdfRequestComplete(request, status);

			return;
		}

		// Check if the input buffer size is correct
		if (bufferLength != inputBufferLength) {
			if (Configuration::debugPrint)
				DbgPrintEx(0, 0, "[RFID Reader] %s: Input buffer length doesn't match the actual buffer length\n", __FUNCTION__);

			WdfRequestComplete(request, STATUS_INVALID_BUFFER_SIZE);

			return;
		}

		// Hook the keyboard class service callback
		devExt->upperConnectData = *connectData;

		connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(device);
		connectData->ClassService = keyboardClassServiceCallback;

		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] %s: Hooked the keyboard class service callback for device\n", __FUNCTION__);
	}

	// If applicable, forward the request to other drivers in the stack, otherwise complete it
	WDF_REQUEST_SEND_OPTIONS sendOptions {};
	WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
	const auto requestForwarded = WdfRequestSend(request, WdfDeviceGetIoTarget(device), &sendOptions);

	if (!requestForwarded) {
		WdfRequestComplete(request, WdfRequestGetStatus(request));
	}
}

NTSTATUS initializeDevice(WDFDEVICE& device, PWDFDEVICE_INIT deviceInit) {
	// Set the device as a filter and create it
	WDF_OBJECT_ATTRIBUTES deviceAttributes {};
	WdfFdoInitSetFilter(deviceInit);
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DeviceExtension);
	auto status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &device);
	if (!NT_SUCCESS(status)) {
		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] %s: Failed to create a device object: 0x%X\n", __FUNCTION__, status);

		return status;
	}

	// Setup an internal device control routine for the device
	WDF_IO_QUEUE_CONFIG ioQueueConfig {};
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);
	ioQueueConfig.EvtIoInternalDeviceControl = reinterpret_cast<decltype(ioQueueConfig.EvtIoInternalDeviceControl)>(internalDeviceControl);
	status = WdfIoQueueCreate(device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {
		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] %s: Failed to set an internal device control routine for a device: 0x%X\n",
				__FUNCTION__, status);
	}

	// Create a queue for the keyboard request
	const auto queue = &FilterGetDeviceExt(device)->keyboardRequestQueue;
	WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchManual);
	status = WdfIoQueueCreate(device, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, queue);
	if (!NT_SUCCESS(status)) {
		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] %s: Failed to create the keyboard request queue for a device: 0x%X\n",
				__FUNCTION__, status);

		return status;
	}

	return status;
}