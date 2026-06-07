#include "device/Device.h"
#include "cmd/CommandQueue.h"
#include "resource/Shader.h"

namespace bgl
{
	ShaderHandle
	IDevice::CreateShader(std::string_view sv) const
	{
		auto desc      = ShaderDesc();
		desc.bytecode  = core::file::readFileBytes(sv);
		desc.debugName = sv;
		return CreateShader(std::move(desc));
	}

	CommandQueueHandle
	IDevice::CreateGraphicsCommandQueue() const
	{
		return CreateCommandQueue(QueueType::kGraphics);
	}
}
