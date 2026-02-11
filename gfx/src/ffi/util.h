#pragma once
#include "GfxBase.h"
#include "error/GfxException.h"
#include "types/LayerType.h"
#include <gfx/ffi/common.h>
#include <gfx/ffi/material.h>

namespace gfx::ffi
{
	template <typename T>
	T*
	gfxObjCast(GfxObj obj)
	{
		auto* data = reinterpret_cast<GfxBase*>(obj.ptr);

		if (!data)
		{
			setLastErrorInfo(
				GFX_RESULT_ERROR_INVALID_ARGUMENT,
				"Invalid Argument",
				"GfxObj is invalid pointer.");

			return nullptr;
		}
		else
		{
			auto* ret = dynamic_cast<T*>(data);
			if (!ret)
			{
				setLastErrorInfo(
					GFX_RESULT_ERROR_INVALID_ARGUMENT,
					"Invalid Argument",
					"GfxObj data pointer is of incorrect type.");

				return nullptr;
			}
			return ret;
		}
	}

	void
	deleteThunk(GfxObj obj);

	[[nodiscard]]
	bool
	validatePtr(void* ptr, std::string_view name);

	template <typename Fn>
	GfxResult
	apiInvoke(Fn&& fn, bool requireInitialzed = true)
	{
		try
		{
			if (requireInitialzed && !isGfxInitialized())
			{
				return setLastErrorInfo(
					GFX_RESULT_ERROR_NOT_INITIALIZED,
					"Not Initialized",
					"GFX API has not been initialized. Call initializeGfx() first.");
			}

			return fn();
		}
		catch (const std::bad_alloc&)
		{
			return setLastErrorInfo(
				GFX_RESULT_ERROR_OUT_OF_MEMORY,
				"Out of Memory",
				"Out of Memory");
		}
		catch (const core::except::BerniniException& ex)
		{
			return setLastErrorInfo(GFX_RESULT_ERROR_UNKNOWN, ex.Title().data(), ex.Body().data());
		}
		catch (const std::exception& ex)
		{
			return setLastErrorInfo(GFX_RESULT_ERROR_UNKNOWN, "C++ Exception", ex.what());
		}
		catch (...)
		{
			return GFX_RESULT_ERROR_UNKNOWN;
		}
	}

	constexpr LayerType
	alphaMode2LayerType(MaterialAlphaMode mode)
	{
		switch (mode)
		{
		case AlphaMode_Opaque:
			return LayerType::kOpaque;
		case AlphaMode_Mask:
			return LayerType::kAlphaTest;
		case AlphaMode_Blend:
			return LayerType::kTransparent;
		default:
			return LayerType::kInvalid;
		}
	}

	inline glm::vec4
	toGlmVec4(GfxVec4 vec4) noexcept
	{
		return glm::vec4{ vec4.x, vec4.y, vec4.z, vec4.w };
	}

	inline glm::vec3
	toGlmVec3(GfxVec3 vec3) noexcept
	{
		return glm::vec3{ vec3.x, vec3.y, vec3.z };
	}

}
