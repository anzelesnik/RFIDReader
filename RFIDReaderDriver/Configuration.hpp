#pragma once

#include <cstdint>

namespace Configuration {
	constexpr auto debugPrint = true;

	constexpr auto filterDeviceId        = L"{0DBBB5AA-66FC-11EA-BC55-0242AC130003}\\RFIDReader";
	constexpr GUID filterDeviceInterface = { 0x39AD5308, 0x66FC, 0x11EA, { 0xBC, 0x55, 0x02, 0x42, 0xAC, 0x13, 0x00, 0x03 } };

	constexpr auto IoctlRequestReaderData = CTL_CODE(FILE_DEVICE_KEYBOARD, 0x1337, METHOD_BUFFERED, FILE_ANY_ACCESS);

	constexpr std::uint16_t enterScanCode = 0x1C;
}