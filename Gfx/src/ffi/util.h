#pragma once
#include "GfxBase.h"
#include "GfxException.h"
#include <gfx/common.h>

namespace gfx::ffi
{
	template <typename T>
	T&
	gfxObjCast(GfxObj obj)
	{
		auto* data = reinterpret_cast<GfxBase*>(obj.data);

		if (!data)
		{
			throw GfxException{ GFX_RESULT_ERROR_INVALID_ARGUMENT,
				                "Invalid Argument",
				                "GfxObj is invalid pointer." };
		}
		else
		{
			auto* ret = dynamic_cast<T*>(data);
			if (!ret)
			{
				throw GfxException{ GFX_RESULT_ERROR_INVALID_ARGUMENT,
					                "Invalid Argument",
					                "GfxObj data pointer is of incorrect type." };
			}
			return *ret;
		}
	}

	void
	deleteThunk(GfxObj obj);

	void
	validatePtr(void* ptr, std::string_view name);

	template <typename Fn>
	GfxResult
	apiInvoke(Fn&& fn, bool requireInitialzed = true)
	{
		try
		{
			if (requireInitialzed && !isGfxInitialized())
			{
				return GfxException::SetLastErrorInfo(
					{ GFX_RESULT_ERROR_NOT_INITIALIZED,
				      "Not Initialized",
				      "GFX API has not been initialized. Call initializeGfx() first." });
			}

			return fn();
		}
		catch (const std::bad_alloc&)
		{
			return GfxException::SetLastErrorInfo(
				{ GFX_RESULT_ERROR_OUT_OF_MEMORY, "Out of Memory", "Out of Memory" });
		}
		catch (const gfx::GfxException& ex)
		{
			return ex.GetErrorResult();
		}
		catch (...)
		{
			return GFX_RESULT_ERROR_UNKNOWN;
		}
	}
}
