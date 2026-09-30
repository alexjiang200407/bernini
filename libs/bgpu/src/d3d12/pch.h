#pragma once

#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

namespace wrl = Microsoft::WRL;

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <bgpu/d3d12/D3d12ErrorChecker.h>

#include <slang-com-ptr.h>
#include <slang.h>

#include <bgpu/SlangErrorChecker.h>
