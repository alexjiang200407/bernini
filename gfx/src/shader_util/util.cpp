#include "shader_util/util.h"
#include <core/file/file.h>

namespace gfx
{
	nvrhi::ShaderHandle
	createShaderFromFile(
		nvrhi::DeviceHandle device,
		const std::string&  filePath,
		nvrhi::ShaderType   shaderType,
		const std::string&  debugName)
	{
		auto bytes = core::file::readFileBytes(filePath);
		auto desc  = nvrhi::ShaderDesc{}
		                .setShaderType(shaderType)
		                .setDebugName(debugName.empty() ? filePath : debugName);

		return device->createShader(desc, bytes.data(), bytes.size());
	}
}
