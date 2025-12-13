#pragma once
#include "buffer/DynamicBuffer.h"

namespace gfx
{
	class DynamicVertexBuffer : public DynamicBuffer
	{
	public:
		DynamicVertexBuffer(nvrhi::DeviceHandle, DynamicBufferDesc elementDesc, uint32_t count) :
			DynamicBuffer{ elementDesc, count }
		{}
	};
}
