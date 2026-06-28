#pragma once
#include "pipeline/MeshletPipeline.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	// A meshlet pipeline paired with the uniforms for every constant buffer it declares
	// (keyed by buffer name). Created by IDevice::CreateMeshletKernel. The map is empty for
	// a shader with no constant buffers.
	struct MeshletKernel
	{
		MeshletKernel()                         = default;
		MeshletKernel(const MeshletKernel&)     = delete;
		MeshletKernel(MeshletKernel&&) noexcept = default;

		MeshletKernel&
		operator=(const MeshletKernel&) = delete;

		MeshletKernel&
		operator=(MeshletKernel&&) noexcept = default;

		core::SharedRef<IMeshletPipeline>         pipeline;
		std::unordered_map<std::string, Uniforms> uniforms;

		// Access a constant buffer's uniforms by name; throws if the shader has no such buffer.
		Uniforms&
		operator[](const std::string& cbuffer)
		{
			return uniforms.at(cbuffer);
		}

		void
		Reset()
		{
			pipeline.Reset();
			uniforms.clear();
		}
	};
}
