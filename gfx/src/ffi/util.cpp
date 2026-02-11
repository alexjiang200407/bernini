#include "ffi/util.h"
#include "util.h"

namespace gfx::ffi
{
	void
	deleteThunk(GfxObj obj)
	{
		auto* data = reinterpret_cast<GfxBase*>(obj.ptr);
		if (data)
		{
			delete data;
		}
		else
		{
			logger::error("Attempted to delete bad GfxObj");
		}
	}

	bool
	validatePtr(void* ptr, std::string_view name)
	{
		if (ptr == nullptr)
		{
			setLastErrorInfo(
				GFX_RESULT_ERROR_INVALID_ARGUMENT,
				"Invalid Argument",
				std::format("<{}> cannot be nullptr", name));
			return false;
		}
		return true;
	}
}
