#include "device/Device.h"
#include "cmd/CommandQueue.h"
#include "resource/Shader.h"

namespace bgl
{
	ShaderHandle
	IDevice::CreateShader(std::string path, std::string moduleName, std::string entryPointName)
		const
	{
		auto desc            = ShaderDesc();
		desc.bytecode        = core::file::readFileBytes(path);
		desc.entryPointName  = std::move(entryPointName);
		desc.debugName       = std::move(path);
		desc.slangModuleName = std::move(moduleName);

		return CreateShader(std::move(desc));
	}

	CommandQueueHandle
	IDevice::CreateGraphicsCommandQueue() const
	{
		return CreateCommandQueue(QueueType::kGraphics);
	}
}
