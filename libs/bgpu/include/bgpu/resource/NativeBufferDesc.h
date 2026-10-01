#pragma once
#include <bgpu/resource/Buffer.h>
#include <bgpu/types/NativeObject.h>
#include <utility>

namespace bgpu
{
	/**
	 * A buffer made outside the manager that imports it -- another owner's, on the same native
	 * device: its native object, read from IResourceManager::GetNativeBuffer, and the structured
	 * buffer the importer views it as (IResourceManager::ImportNativeBuffer). The object is
	 * borrowed; only an import adds a reference.
	 */
	struct NativeBufferDesc
	{
		NativeObjectType type{};
		NativeObject     object;
		StructBufferDesc buffer = StructBufferDesc().SetDebugName("Imported Buffer");

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

		template <typename Self>
		Self&&
		SetBuffer(this Self&& self, StructBufferDesc value) noexcept
		{
			self.buffer = std::move(value);
			return std::forward<Self>(self);
		}
	};
}
