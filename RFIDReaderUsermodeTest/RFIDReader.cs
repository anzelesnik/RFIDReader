using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace RFIDReaderUsermodeTest
{
    public static class RFIDReader
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct SP_DEVICE_INTERFACE_DATA
        {
            private int Size;
            private Guid InterfaceClassGuid;
            private int Flags;
            private IntPtr Reserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct SP_DEVICE_INTERFACE_DETAIL_DATA
        {
            private int Size;
            private char DevicePath;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern SafeFileHandle CreateFile(
            string FileName,
            uint DesiredAccess,
            uint ShareMode,
            uint SecurityAttributes,
            uint CreationDisposition,
            uint FlagsAndAttributes,
            IntPtr TemplateFile
        );

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool DeviceIoControl(
            SafeFileHandle Device,
            uint IoControlCode,
            [MarshalAs(UnmanagedType.AsAny)]
            [In] object InBuffer,
            uint InBufferSize,
            [MarshalAs(UnmanagedType.AsAny)]
            [Out] object OutBuffer,
            uint OutBufferSize,
            ref uint BytesReturned,
            [In] IntPtr Overlapped
        );

        [DllImport("user32.dll")]
        private static extern uint MapVirtualKey(uint uCode, uint uMapType);

        [DllImport("setupapi.dll", SetLastError = true)]
        private static extern SafeFileHandle SetupDiGetClassDevs(
            ref Guid ClassGuid,
            string Enumerator,
            IntPtr Parent,
            uint Flags
        );

        [DllImport("setupapi.dll", SetLastError = true)]
        private static extern bool SetupDiEnumDeviceInterfaces(
            SafeFileHandle DevInfoSet,
            IntPtr DevInfoData,
            ref Guid InterfaceClassGuid,
            uint MemberIndex,
            IntPtr DeviceInterfaceData
        );

        [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern bool SetupDiGetDeviceInterfaceDetail(
            SafeFileHandle DevInfoSet,
            IntPtr DeviceInterfaceData,
            IntPtr DeviceInterfaceDetailData,
            uint deviceInterfaceDetailDataSize,
            ref uint RequiredSize,
            IntPtr DeviceInfoData
        );

        [DllImport("setupapi.dll", SetLastError = true)]
        public static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

        private const uint FILE_ATTRIBUTE_NORMAL = 0x80;
        private const uint FILE_SHARE_READ       = 0x1;
        private const uint FILE_SHARE_WRITE      = 0x2;
        private const uint GENERIC_READ          = 0x80000000;
        private const uint GENERIC_WRITE         = 0x40000000;
        private const uint OPEN_EXISTING         = 0x3;

        private const uint DIGCF_PRESENT         = 0x2;
        private const uint DIGCF_DEVICEINTERFACE = 0x10;

        private const uint MAPVK_VSC_TO_VK  = 0x1;
        private const uint MAPVK_VK_TO_CHAR = 0x2;

        // This GUID must match the one that the driver uses
        private static Guid filterDeviceInterface = new Guid(
            0x39AD5308, 0x66FC, 0x11EA, 0xBC, 0x55, 0x02, 0x42, 0xAC, 0x13, 0x00, 0x03);
        private const uint IoctlRequestReaderData = 0xB4CDC;

        private static string GetFilterDeviceInterface(uint deviceIndex)
        {
            // Open a handle to the information set of the filter device interfaces
            // TODO Figure out why the VS debugger is reporting an external exception some time after this call
            var deviceInfoSet = SetupDiGetClassDevs(ref filterDeviceInterface, null,
                IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

            if (deviceInfoSet.IsInvalid)
                return string.Empty;

            // Retrieve the first interface information
            var interfaceData = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA)));
            Marshal.WriteInt32(interfaceData, Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA))); // Write the size of the structure
            var status = SetupDiEnumDeviceInterfaces(deviceInfoSet, IntPtr.Zero, ref filterDeviceInterface,
                deviceIndex, interfaceData);
            if (!status)
            {
                Marshal.FreeHGlobal(interfaceData);
                SetupDiDestroyDeviceInfoList(deviceInfoSet.DangerousGetHandle());
                return string.Empty;
            }

            // Retrieve the interface information buffer size
            uint requiredSize = 0;
            SetupDiGetDeviceInterfaceDetail(deviceInfoSet, interfaceData, IntPtr.Zero,
                0, ref requiredSize, IntPtr.Zero);
            if (requiredSize == 0)
            {
                Marshal.FreeHGlobal(interfaceData);
                SetupDiDestroyDeviceInfoList(deviceInfoSet.DangerousGetHandle());
                return string.Empty;
            }

            // Retrieve the interface path
            // TODO Not completely sure why the buffer can't be marshaled into a structure (difference in size?)
            var detailData = Marshal.AllocHGlobal((IntPtr)requiredSize);
            Marshal.WriteInt32(detailData, Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DETAIL_DATA))); // Write the size of the structure
            status = SetupDiGetDeviceInterfaceDetail(deviceInfoSet, interfaceData, detailData,
                requiredSize, ref requiredSize, IntPtr.Zero);
            SetupDiDestroyDeviceInfoList(deviceInfoSet.DangerousGetHandle());
            if (!status)
            {
                Marshal.FreeHGlobal(interfaceData);
                Marshal.FreeHGlobal(detailData);
                return string.Empty;
            }

            string devicePath = Marshal.PtrToStringAnsi(detailData + 0x4);

            Marshal.FreeHGlobal(detailData);

            return devicePath;
        }

        public static string RequestReaderData(uint deviceIndex)
        {
            // Retrieve the interface path for the device at the specified index
            var filterDevice = GetFilterDeviceInterface(deviceIndex);
            if (filterDevice == string.Empty)
                return "Error";

            // Open a handle to the filter device
            var deviceHandle = CreateFile(filterDevice, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
            if (deviceHandle.IsInvalid)
                return "Error";

            // Queue a request to the filter device to retrieve the RFID card data on next scan
            // Generally, overlapped IO should be used here, the application might crash because it's not
            byte[] buffer = new byte[11];
            uint bytesReturned = 0;
            var status = DeviceIoControl(deviceHandle, IoctlRequestReaderData, null, 0, buffer,
                (uint)buffer.Length, ref bytesReturned, IntPtr.Zero);
            deviceHandle.Close();
            if (!status || bytesReturned == 0)
                return "Error";

            // Translate the scan codes into ASCII characters 
            for (var i = 0; i < bytesReturned; ++i)
            {
                // Translate the scan code into a virtual key code
                buffer[i] = (byte)MapVirtualKey(buffer[i], MAPVK_VSC_TO_VK);
                // Generally not needed as characters from A-Z and 0-9 always correspond to their virtual key codes
                buffer[i] = (byte)MapVirtualKey(buffer[i], MAPVK_VK_TO_CHAR);
            }

            return System.Text.Encoding.ASCII.GetString(buffer);
        }
    }
}
