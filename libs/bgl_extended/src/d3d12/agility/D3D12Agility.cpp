#include <Windows.h>

// The D3D12 Agility SDK requires the *host executable* to export these symbols so
// the D3D12 runtime can locate the redistributable core DLLs (copied to .\D3D12\
// next to the exe). Because they must live in the .exe and not in the bgl DLL,
// this is built as the bgl_d3d12_agility OBJECT library and linked into every
// d3d12 executable (see bgl/src/d3d12/CMakeLists.txt). UINT / LPCSTR come from
// <Windows.h> so this stays self-contained without the bgl precompiled header.
extern "C"
{
	__declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
	__declspec(dllexport) extern LPCSTR     D3D12SDKPath    = ".\\";
}
