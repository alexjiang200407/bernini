#pragma once
#include <cstdint>

namespace bgpu
{
	/**
	 * Which native object a `GetNativeObject` call asks for. An object answers only for the types its
	 * backend has, and a null `NativeObject` for the rest.
	 */
	enum class NativeObjectType : uint8_t
	{
		kD3D12Device,
		kD3D12CommandQueue,
		kD3D12GraphicsCommandList,
		kD3D12Resource,
		kMtlDevice,
		kMtlCommandQueue,
		kMtlCommandBuffer,
		kMtlTexture,
		kMtlBuffer,
		kVkBuffer,
	};

	/**
	 * A backend's own object behind an RHI one, untyped so that no RHI header names a backend type:
	 * the caller that asked for a `NativeObjectType` knows what it holds and says so with `As<T>`.
	 * Borrowed: the RHI object keeps it alive, and a caller that needs it longer adds its own
	 * reference.
	 */
	struct NativeObject
	{
		void* pointer = nullptr;

		template <typename T>
		[[nodiscard]] T*
		As() const noexcept
		{
			return static_cast<T*>(pointer);
		}

		[[nodiscard]] explicit
		operator bool() const noexcept
		{
			return pointer != nullptr;
		}
	};
}
