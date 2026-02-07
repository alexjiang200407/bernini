#include "buffer/ComputeBuffer.h"

namespace gfx
{
	void
	ComputeBuffer::Init(nvrhi::DeviceHandle device, const ComputeBufferDesc& desc)
	{
		if (desc.elemCount == 0 || desc.elemSize == 0)
		{
			throw GfxException{ GFX_RESULT_ERROR_COMPUTE_BUFFER,
				                "Compute Buffer error",
				                "ComputeBuffer: element count and size must be greater than zero" };
		}

		m_elementCount = desc.elemCount;
		m_elementSize  = desc.elemSize;

		auto bufferDesc = nvrhi::BufferDesc{};
		bufferDesc.setStructStride(desc.elemSize)
			.setByteSize(static_cast<uint64_t>(desc.elemSize) * desc.elemCount)
			.setCanHaveUAVs(true)
			.setDebugName(desc.debugName)
			.setInitialState(desc.initialState)
			.setKeepInitialState(desc.keepInitialState);
		m_buffer = device->createBuffer(bufferDesc);
	}
}
