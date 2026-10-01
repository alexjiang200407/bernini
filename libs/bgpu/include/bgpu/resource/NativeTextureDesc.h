#pragma once
#include <bgpu/resource/Texture.h>
#include <bgpu/types/NativeObject.h>
#include <utility>

namespace bgpu
{
	/**
	 * A texture made outside the manager that imports it -- a swapchain's backbuffer: its native
	 * object and the texture the importer views it as (IResourceManager::ImportNativeTexture). The
	 * object is borrowed; only an import adds a reference.
	 */
	struct NativeTextureDesc
	{
		NativeObjectType type{};
		NativeObject     object;
		TextureDesc      texture;

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
		SetTexture(this Self&& self, TextureDesc value) noexcept
		{
			self.texture = std::move(value);
			return std::forward<Self>(self);
		}
	};
}
