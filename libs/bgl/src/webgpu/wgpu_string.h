#pragma once

namespace bgl::wgpu
{
	/**
	 * Copies a WebGPU string view into an owned string.
	 *
	 * A WGPUStringView is a (data, length) pair that does not own its bytes and is only valid
	 * for the duration of the callback that produced it, so anything outliving that call must
	 * copy. A null data pointer and the WGPU_STRLEN sentinel (meaning "NUL-terminated") are
	 * both handled.
	 */
	inline std::string
	ToString(WGPUStringView view) noexcept
	{
		if (view.data == nullptr)
			return {};

		return view.length == WGPU_STRLEN ? std::string(view.data) :
		                                    std::string(view.data, view.length);
	}

	/**
	 * Borrows a string as a WebGPU string view.
	 *
	 * @pre str outlives every WebGPU call the returned view is passed to.
	 */
	inline WGPUStringView
	ToStringView(std::string_view str) noexcept
	{
		return WGPUStringView{ str.data(), str.size() };
	}
}
