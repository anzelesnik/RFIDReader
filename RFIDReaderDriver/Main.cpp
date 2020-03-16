#include "Nt.hpp"
#include "Configuration.hpp"
#include "Public.hpp"

#include <cstddef>
#include <cstdint>

// Used for assigning different ID's to devices when added
std::uint32_t deviceInstanceIndex {};

// Buffer size depends on the size of the data stored on a RFID chip
// 64 bytes should be enough for a normal ID card
std::uint8_t readerBuffer[64]   {};
std::uint32_t readerBufferIndex {};

void keyboardClassServiceCallback(PDEVICE_OBJECT deviceObject, PKEYBOARD_INPUT_DATA inputDataStart,
                     PKEYBOARD_INPUT_DATA inputDataEnd, std::uint32_t *inputDataConsumed) {
	if (inputDataStart->Flags != KEY_MAKE)
		return;

	if (readerBufferIndex > sizeof(readerBuffer) - 1) {
		// Buffer filled, reset it to prevent a buffer overflow
		memset(readerBuffer, 0, sizeof(readerBuffer));
		readerBufferIndex = 0;
	}

	// The RFID reader submits an enter scan code at the end of each buffer 
	if (inputDataStart->MakeCode != Configuration::enterScanCode) {
		readerBuffer[readerBufferIndex] = reinterpret_cast<std::uint8_t*>(&inputDataStart->MakeCode)[0];
		readerBufferIndex++;
		return;
	}

	// Retrieve the request from the queue of user mode requests
	WDFREQUEST request {};
	NTSTATUS status    {};
	const auto device = WdfWdmDeviceGetWdfDeviceHandle(deviceObject);
	while (status = WdfIoQueueRetrieveNextRequest(FilterGetDeviceExt(device)->keyboardRequestQueue, &request), NT_SUCCESS(status)) {
		// Retrieve the request output buffer supplied by the user mode application
		void* requestBuffer{};
		std::size_t requestBufferSize{};
		status = WdfRequestRetrieveOutputBuffer(request, sizeof(ReaderData), &requestBuffer, &requestBufferSize);
		if (!NT_SUCCESS(status)) {
			if (Configuration::debugPrint)
				DbgPrintEx(0, 0, "[RFID Reader] %s: Failed to retrieve the request output buffer: 0x%X\n", __FUNCTION__, status);

			WdfRequestComplete(request, STATUS_INVALID_PARAMETER);

			continue;
		}

		if (!requestBufferSize) {
			if (Configuration::debugPrint)
				DbgPrintEx(0, 0, "[RFID Reader] %s: User did not provide a buffer", __FUNCTION__);

			WdfRequestComplete(request, STATUS_INVALID_PARAMETER);

			continue;
		}

		// Check if user supplied a big enough buffer, otherwise truncate the data
		const auto bufferCopySize = readerBufferIndex > requestBufferSize ? requestBufferSize : readerBufferIndex;

		// TODO Figure out what happens in the case that the input buffer size is bigger than he actual amount of available memory
		// Copy the reader buffer to the request output buffer and set the length that was copied
		memcpy(requestBuffer, readerBuffer, bufferCopySize);
		WdfRequestSetInformation(request, bufferCopySize);

		// Set the request status appropriately
		status = bufferCopySize < readerBufferIndex ? STATUS_PARTIAL_COPY : STATUS_SUCCESS;

		WdfRequestComplete(request, status);
	}

	// Reset the reader buffer
	memset(readerBuffer, 0, sizeof(readerBuffer));
	readerBufferIndex = 0;
}

NTSTATUS evtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit) {
	WDFDEVICE device {};

	// Initialize the primary filter device and its IO queues
	auto status = initializeDevice(device, deviceInit);
	if (!NT_SUCCESS(status)) {
		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] %s: Failed to create a device: 0x%X\n", __FUNCTION__, status);

		return status;
	}

	// Due to the RIT having exclusive control over the keyboard devices,
	// we need to create a raw PDO in order to send requests to the device
	status = initializeDeviceRawPdo(device, deviceInstanceIndex++);
	if (!NT_SUCCESS(status)) {
		if (Configuration::debugPrint)
			DbgPrintEx(0, 0, "[RFID Reader] %s: Failed to create the raw PDO for a device: 0x%X\n", __FUNCTION__, status);

		return status;
	}
	
	if (Configuration::debugPrint)
		DbgPrintEx(0, 0, "[RFID Reader] Device added\n");
	
	return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
	WDF_DRIVER_CONFIG driverConfig {};

	WDF_DRIVER_CONFIG_INIT(&driverConfig, evtDeviceAdd);

	// Create a WDF driver object
	const auto status = WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES,
		&driverConfig, WDF_NO_HANDLE);

	if (Configuration::debugPrint)
		DbgPrintEx(0, 0, "[RFID Reader] Driver loaded\n");

	return STATUS_SUCCESS;
}