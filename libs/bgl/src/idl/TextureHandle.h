// THIS IS A FILE GENERATED FROM TextureHandle.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct TextureHandle
	{
		DescriptorHandle texture;
	};

	static_assert(sizeof(TextureHandle) == 8);
	static_assert(offsetof(TextureHandle, texture) == 0);

	struct TextureCubeHandle
	{
		DescriptorHandle texture;
	};

	static_assert(sizeof(TextureCubeHandle) == 8);
	static_assert(offsetof(TextureCubeHandle, texture) == 0);

}
