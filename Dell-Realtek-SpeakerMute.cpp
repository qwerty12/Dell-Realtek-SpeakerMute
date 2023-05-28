#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <initguid.h>
#include <SetupAPI.h>
#include <winioctl.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>

#define VEN_REALTEK L"10EC"
#define IOCTL_REALTEK_SET_EVENT 0x225C18
struct __declspec(uuid("{6128A8C4-6C26-4373-B630-063759AA5141}")) PIN_PROPERTY_GUID;
/* This is undocumented, and could stop working with Windows 22, but the rest of this is running off assumptions...
   This is much quicker than the proper ways:
	- https://www.freelists.org/post/wdmaudiodev/Getting-hardware-ID-from-endpointID,1
	- https://matthewvaneerde.wordpress.com/2008/06/13/sample-find-out-if-your-default-audio-playback-and-audio-capture-devices-are-on-the-same-hardware/
	- https://matthewvaneerde.wordpress.com/2011/06/13/how-to-enumerate-audio-endpoint-immdevice-properties-on-your-system/
	- https://matthewvaneerde.wordpress.com/2014/11/20/walking-the-idevicetopology-tree-to-see-audio-driver-settings/
*/
DEFINE_PROPERTYKEY(PKKEY_DeviceTopology_DeviceId, 0x233164c8, 0x1b2c, 0x4c7d, 0xbc, 0x68, 0xb6, 0x71, 0x68, 0x7a, 0x25, 0x67, 1);
// Change as required for your device
#define DEST_INTERFACE L"REARLINEOUTWAVE"
#define PIN_PROPERTY_ID 4

typedef struct
{
	HANDLE DriverSignalledEvent;
	HANDLE unk; // 8 bytes on x64; might not be a HANDLE
} driver_set_event_data;

typedef struct
{
	USHORT plug;
	CHAR unk[122];
} pin_ioctl_result;

static PSP_DEVICE_INTERFACE_DETAIL_DATA_W FindRealtekAudioDevice()
{
	PSP_DEVICE_INTERFACE_DETAIL_DATA_W ret = NULL;
	LPCWSTR errMsg = NULL;

	DWORD RequiredSize;
	HDEVINFO ClassDevs;
	SP_DEVINFO_DATA DeviceInfoData;
	SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;

	ClassDevs = SetupDiGetClassDevsW(&KSCATEGORY_AUDIO, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (ClassDevs == INVALID_HANDLE_VALUE) {
		errMsg = L"SetupDiGetClassDevsW == INVALID_HANDLE_VALUE";
		goto end;
	}

	DeviceInfoData.cbSize = sizeof(DeviceInfoData);
	for (DWORD i = 0;; ++i) {
		if (!SetupDiEnumDeviceInfo(ClassDevs, i, &DeviceInfoData)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				errMsg = L"Couldn't find Realtek audio device";
				goto end;
			}
			else {
				continue;
			}
		}

		SetupDiGetDeviceInstanceIdW(ClassDevs, &DeviceInfoData, NULL, 0, &RequiredSize);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			continue;

		LPWSTR DeviceInstanceId = (LPWSTR)_malloca(RequiredSize * sizeof(WCHAR));
		if (!DeviceInstanceId) {
			errMsg = L"_malloca failed";
			goto end;
		}

		if (SetupDiGetDeviceInstanceIdW(ClassDevs, &DeviceInfoData, DeviceInstanceId, RequiredSize, NULL)) {
			_wcsupr_s(DeviceInstanceId, RequiredSize);
			if (wcsstr(DeviceInstanceId, VEN_REALTEK)) {
				_freea(DeviceInstanceId);
				break;
			}
		}

		_freea(DeviceInstanceId);
	}

	DeviceInterfaceData.cbSize = sizeof(DeviceInterfaceData);
	for (DWORD i = 0;; ++i) {
		if (!SetupDiEnumDeviceInterfaces(ClassDevs, &DeviceInfoData, &KSCATEGORY_AUDIO, i, &DeviceInterfaceData)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				errMsg = L"Couldn't find " DEST_INTERFACE L" on Realtek audio device";
				goto end;
			}
			else {
				continue;
			}
		}

		SetupDiGetDeviceInterfaceDetailW(ClassDevs, &DeviceInterfaceData, NULL, 0, &RequiredSize, NULL);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			continue;

		PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)LocalAlloc(GPTR, RequiredSize);
		if (!DeviceInterfaceDetailData) {
			errMsg = L"LocalAlloc failed";
			goto end;
		}
		DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
		if (SetupDiGetDeviceInterfaceDetailW(ClassDevs, &DeviceInterfaceData, DeviceInterfaceDetailData, RequiredSize, NULL, NULL)) {
			_wcsupr_s(DeviceInterfaceDetailData->DevicePath, (RequiredSize - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) / sizeof(WCHAR));
			if (wcsstr(DeviceInterfaceDetailData->DevicePath, DEST_INTERFACE)) {
				ret = DeviceInterfaceDetailData;
				break;
			}
		}

		LocalFree(DeviceInterfaceDetailData);
	}

end:
	if (errMsg)
		OutputDebugStringW(errMsg);

	if (ClassDevs != INVALID_HANDLE_VALUE)
		SetupDiDestroyDeviceInfoList(ClassDevs);

	return ret;
}

static FORCEINLINE BOOL HeadphonesPluggedIn(CONST HANDLE audioDevice)
{
	static KSPROPERTY propPinStatus =
	{
		__uuidof(PIN_PROPERTY_GUID),
		PIN_PROPERTY_ID,
		KSPROPERTY_TYPE_GET
	};
	static pin_ioctl_result pinStatusResult;
	DWORD BytesReturned;

	pinStatusResult.plug = 0;
	if (!DeviceIoControl(audioDevice, IOCTL_KS_PROPERTY, &propPinStatus, sizeof(propPinStatus), &pinStatusResult, sizeof(pinStatusResult), &BytesReturned, NULL) || BytesReturned < sizeof(pinStatusResult.plug)) {
		OutputDebugStringW(L"DeviceIoControl IOCTL_KS_PROPERTY failure");
		ExitProcess(EXIT_FAILURE);
	}

	return (((pinStatusResult.plug + 256) >> 1) & 1) != FALSE;
}

static IAudioEndpointVolume* FindRealtekAudioEndpoint(LPCWSTR HardwareId)
{
	IAudioEndpointVolume* ret = NULL;

	IMMDeviceEnumerator* pMMDeviceEnumerator;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), reinterpret_cast<LPVOID*>(&pMMDeviceEnumerator)))) {
		OutputDebugStringW(L"CoCreateInstance failed");
		ExitProcess(EXIT_FAILURE);
		return NULL;
	}

	IMMDeviceCollection* pMMDeviceCollection;
	if (FAILED(pMMDeviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection))) {
		OutputDebugStringW(L"EnumAudioEndpoints failed");
		ExitProcess(EXIT_FAILURE);
		return NULL;
	}

	UINT deviceCount;
	if (FAILED(pMMDeviceCollection->GetCount(&deviceCount)) || !deviceCount) {
		OutputDebugStringW(L"Apparently no active render devices");
		ExitProcess(EXIT_FAILURE);
		return NULL;
	}

	for (UINT i = 0; !ret && i < deviceCount; ++i) {
		IMMDevice* pRenderEndpoint;
		if (FAILED(pMMDeviceCollection->Item(i, &pRenderEndpoint)))
			continue;

		IPropertyStore* pRenderDevnodePropertyStore;
		if (SUCCEEDED(pRenderEndpoint->OpenPropertyStore(STGM_READ, &pRenderDevnodePropertyStore))) {
			PROPVARIANT var;
			PropVariantInit(&var);

			if (SUCCEEDED(pRenderDevnodePropertyStore->GetValue(PKKEY_DeviceTopology_DeviceId, &var))) {
				if (var.vt == VT_LPWSTR) {
					_wcsupr_s(var.pwszVal, wcslen(var.pwszVal) + 1);
					if (wcsstr(var.pwszVal, HardwareId)) {
						if (FAILED(pRenderEndpoint->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<LPVOID*>(&ret)))) {
							OutputDebugStringW(L"IID_IAudioEndpointVolume activation failed");
							ExitProcess(EXIT_FAILURE);
							return NULL;
						}
					}
				}

				PropVariantClear(&var);
			}

			pRenderDevnodePropertyStore->Release();
		}

		pRenderEndpoint->Release();
	}

	if (!ret) {
		OutputDebugStringW(L"Couldn't find audio endpoint");
		ExitProcess(EXIT_FAILURE);
		return NULL;
	}

	pMMDeviceCollection->Release();
	pMMDeviceEnumerator->Release();
	return ret;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	PSP_DEVICE_INTERFACE_DETAIL_DATA_W waveDevice = FindRealtekAudioDevice();
	if (!waveDevice)
		return EXIT_FAILURE;

	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY)))
		return EXIT_FAILURE;

	IAudioEndpointVolume* pAudioEndpointVolume = FindRealtekAudioEndpoint(waveDevice->DevicePath);

	driver_set_event_data drvData = { 0 };
	drvData.DriverSignalledEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

	HANDLE audioDevice = CreateFileW(waveDevice->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (audioDevice == INVALID_HANDLE_VALUE) {
		OutputDebugStringW(L"CreateFileW failure");
		return EXIT_FAILURE;
	}

	CHAR out[16]; // Required, but I have no idea what this contains
	if (!DeviceIoControl(audioDevice, IOCTL_REALTEK_SET_EVENT, &drvData, sizeof(drvData), out, sizeof(out), NULL, NULL)) {
		OutputDebugStringW(L"DeviceIoControl IOCTL_REALTEK_SET_EVENT failure");
		return EXIT_FAILURE;
	}

	while (WaitForSingleObject(drvData.DriverSignalledEvent, INFINITE) == WAIT_OBJECT_0) {
		if (!HeadphonesPluggedIn(audioDevice)) {
			if (FAILED(pAudioEndpointVolume->SetMute(TRUE, NULL))) {
				pAudioEndpointVolume->Release();
				pAudioEndpointVolume = FindRealtekAudioEndpoint(waveDevice->DevicePath);
				pAudioEndpointVolume->SetMute(TRUE, NULL);
			}
		}
	}

	CloseHandle(audioDevice);
	CloseHandle(drvData.DriverSignalledEvent);
	pAudioEndpointVolume->Release();
	LocalFree(waveDevice);
	CoUninitialize();

	return EXIT_SUCCESS;
}