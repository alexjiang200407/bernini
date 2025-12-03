#include "GfxHandle.h"

namespace game
{
	GfxHandle::GfxHandle(Bernini_GfxObj handle) noexcept : handle_(handle) {}

	GfxHandle::GfxHandle() noexcept
	{
		handle_.destroy = nullptr;
		handle_.data    = nullptr;
	}

	GfxHandle::GfxHandle(GfxHandle&& other) noexcept : handle_(other.handle_)
	{
		other.handle_ = { nullptr, nullptr };
	}

	GfxHandle&
	GfxHandle::operator=(GfxHandle&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Reset();
			handle_       = other.handle_;
			other.handle_ = { nullptr, nullptr };
		}
		return *this;
	}

	GfxHandle::operator Bernini_GfxObj() const noexcept { return handle_; }

	Bernini_GfxObj
	GfxHandle::Release() noexcept
	{
		Bernini_GfxObj tmp = handle_;
		handle_            = { nullptr, nullptr };
		return tmp;
	}

	void
	GfxHandle::Reset() noexcept
	{
		if (handle_.destroy)
		{
			handle_.destroy(handle_);
		}
		handle_.destroy = nullptr;
		handle_.data    = nullptr;
	}

}
