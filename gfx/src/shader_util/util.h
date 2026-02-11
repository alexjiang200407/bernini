#pragma once

namespace gfx
{
	nvrhi::ShaderHandle
	createShaderFromFile(
		nvrhi::DeviceHandle device,
		const std::string&  filePath,
		nvrhi::ShaderType   shaderType,
		const std::string&  debugName = "");
}
