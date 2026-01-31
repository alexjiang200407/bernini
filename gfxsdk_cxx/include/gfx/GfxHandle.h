#pragma once
#include <gfx/ffi/common.h>

namespace game
{
	class GfxHandle
	{
	public:
		explicit GfxHandle(GfxObj handle) noexcept;

		GfxHandle() noexcept;

		GfxHandle(const GfxHandle&) = delete;

		GfxHandle&
		operator=(const GfxHandle&) = delete;

		GfxHandle(GfxHandle&& other) noexcept;

		GfxHandle&
		operator=(GfxHandle&& other) noexcept;

		operator GfxObj() const noexcept;

		~GfxHandle() { Reset(); }

		GfxObj*
		operator&() noexcept
		{
			return &handle_;
		}

		GfxObj
		Release() noexcept;

		void
		Reset() noexcept;

		bool
		IsValid() const noexcept
		{
			return handle_.destroy != nullptr && handle_.ptr != nullptr;
		}

		GfxObj
		GetNative() const noexcept
		{
			return handle_;
		}

	private:
		GfxObj handle_;
	};

}
