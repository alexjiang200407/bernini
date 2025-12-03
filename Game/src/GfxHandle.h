#pragma once
#include <gfx/gfx.h>

namespace game
{
	class GfxHandle
	{
	public:
		explicit GfxHandle(Bernini_GfxObj handle) noexcept;

		GfxHandle() noexcept;

		GfxHandle(const GfxHandle&) = delete;

		GfxHandle&
		operator=(const GfxHandle&) = delete;

		GfxHandle(GfxHandle&& other) noexcept;

		GfxHandle&
		operator=(GfxHandle&& other) noexcept;

		operator Bernini_GfxObj() const noexcept;

		~GfxHandle() { Reset(); }

		Bernini_GfxObj*
		operator&() noexcept
		{
			return &handle_;
		}

		Bernini_GfxObj
		Release() noexcept;

		void
		Reset() noexcept;

		bool
		IsValid() const noexcept
		{
			return handle_.destroy != nullptr && handle_.data != nullptr;
		}

		Bernini_GfxObj
		GetNative() const noexcept
		{
			return handle_;
		}

	private:
		Bernini_GfxObj handle_;
	};

}
