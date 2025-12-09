#include "ffi/util.h"

namespace gfx::ffi
{
	void
	deleteThunk(GfxObj obj)
	{
		auto* data = reinterpret_cast<GfxBase*>(obj.data);
		if (data)
		{
			delete data;
		}
		else
		{
			logger::error("Attempted to delete bad GfxObj");
		}
	}

	void
	validatePtr(void* ptr, std::string_view name)
	{
		if (ptr == nullptr)
		{
			throw gfx::GfxException{ GFX_RESULT_ERROR_INVALID_ARGUMENT,
				                     "Invalid Argument",
				                     std::format("<{}> cannot be nullptr", name) };
		}
	}
}
