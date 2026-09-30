#pragma once
#include <bgpu/types/NativeObject.h>
#include <core/type_traits.h>
#include <cstdint>
#include <string>
#include <utility>

namespace bgpu
{
	/**
	 * A buffer one owner offers another on the same native device: its native object, read from
	 * IResourceManager::GetNativeBuffer, and the read-only structured layout an importer views it
	 * as (IResourceManager::ImportNativeBuffer).
	 *
	 * A description, not a reference: the object is borrowed, and only an import adds a reference
	 * of its own.
	 */
	struct NativeBufferDesc
	{
		NativeObjectType type{};
		NativeObject     object;
		uint32_t         stride       = 0;
		uint32_t         elementCount = 0;
		std::string      debugName    = "Imported Buffer";

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return !object;
		}

		template <typename Self>
		Self&&
		SetObject(this Self&& self, NativeObjectType type, NativeObject object) noexcept
		{
			self.type   = type;
			self.object = object;
			return std::forward<Self>(self);
		}

		template <core::type_traits::trivially_copyable T, typename Self>
		Self&&
		SetElement(this Self&& self) noexcept
		{
			self.stride = sizeof(T);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetElementCount(this Self&& self, uint32_t count) noexcept
		{
			self.elementCount = count;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string name) noexcept
		{
			self.debugName = std::move(name);
			return std::forward<Self>(self);
		}
	};
}
