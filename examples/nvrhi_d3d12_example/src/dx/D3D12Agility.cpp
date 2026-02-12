#include <Windows.h>

extern "C"
{
	__declspec(dllexport) extern const uint32_t D3D12SDKVersion = 618;
	__declspec(dllexport) extern LPCSTR         D3D12SDKPath    = ".\\D3D12\\";
}
