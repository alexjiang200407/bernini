#pragma once

#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

namespace wrl = Microsoft::WRL;

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <bgpu/d3d12/D3d12ErrorChecker.h>

namespace bgl
{
	using bgpu::c_D3d12ErrChecker;
}

#include <slang-com-ptr.h>
#include <slang.h>

#include <bgl_common/SlangErrorChecker.h>
