// THIS IS A FILE GENERATED FROM ChannelSource.slang. DO NOT EDIT MANUALLY
#pragma once
#include "TextureHandle.h"

namespace bgl::idl
{
	struct ChannelSource
	{
		TextureHandle texture;
		uint16_t channel;
	};

	static_assert(sizeof(ChannelSource) == 8);
	static_assert(offsetof(ChannelSource, texture) == 0);
	static_assert(offsetof(ChannelSource, channel) == 4);

}
