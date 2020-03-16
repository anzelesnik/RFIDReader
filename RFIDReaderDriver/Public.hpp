#pragma once

#include "Nt.hpp"

#include <cstdint>

extern std::uint32_t deviceInstanceIndex;

NTSTATUS initializeDevice(WDFDEVICE& device, PWDFDEVICE_INIT deviceInit);
NTSTATUS initializeDeviceRawPdo(WDFDEVICE device, ULONG deviceIndex);

void keyboardClassServiceCallback(PDEVICE_OBJECT deviceObject, PKEYBOARD_INPUT_DATA inputDataStart,
    PKEYBOARD_INPUT_DATA inputDataEnd, std::uint32_t* inputDataConsumed);

struct DeviceExtension {
	// Used to store the original Keyboard Class Service Callback
	CONNECT_DATA upperConnectData;
	// Used to store the request from user mode
	WDFQUEUE keyboardRequestQueue;
};

struct RawPdoDeviceExtension {
	WDFQUEUE keyboardRequestQueue;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceExtension, FilterGetDeviceExt)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RawPdoDeviceExtension, FilterGetRawPdoDeviceExt)

struct ReaderData {
	char buffer;
};