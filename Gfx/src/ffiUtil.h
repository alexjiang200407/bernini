#pragma once
#include "GfxBase.h"

namespace gfx::ffi
{
	template <typename T>
	T*
	gfxObjCast(Bernini_GfxObj handle)
	{
		auto* base = reinterpret_cast<GfxBase*>(handle.data);

		if (!base)
		{
			throw gfx::RendererException{ BERNINI_GFX_RENDERER_RESULT_ERROR_INVALID_HANDLE,
				                          "Invalid Handle",
				                          "The handle is null." };
		}

		T* retVal = dynamic_cast<T*>(base);

		if (!retVal)
		{
			throw gfx::RendererException{ BERNINI_GFX_RENDERER_RESULT_ERROR_INVALID_HANDLE,
				                          "Invalid Handle",
				                          "The handle does not refer to the expected type" };
		}

		return retVal;
	}

	template <typename T>
	void
	handleDeleteThunk(Bernini_GfxObj handle)
	{
		auto* ptr = static_cast<T*>(handle.data);
		if (ptr)
		{
			delete ptr;
		}
	}
}
