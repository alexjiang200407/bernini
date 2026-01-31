#include <gfx/GfxHandle.h>

namespace game
{
	GfxHandle::GfxHandle(GfxObj handle) noexcept : handle_(handle) {}

	GfxHandle::GfxHandle() noexcept
	{
		handle_.destroy = nullptr;
		handle_.u64     = 0;
	}

	GfxHandle::GfxHandle(GfxHandle&& other) noexcept : handle_(other.handle_)
	{
		other.handle_ = { 0u, 0u };
	}

	GfxHandle&
	GfxHandle::operator=(GfxHandle&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Reset();
			handle_       = other.handle_;
			other.handle_ = { 0u, 0u };
		}
		return *this;
	}

	GfxHandle::
	operator GfxObj() const noexcept
	{
		return handle_;
	}

	GfxObj
	GfxHandle::Release() noexcept
	{
		GfxObj tmp = handle_;
		handle_    = { 0u, 0u };
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
		handle_.u64     = 0;
	}

}
