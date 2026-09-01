#pragma once
#include "pipeline/MeshletPipeline.h"
#include "uniforms/Uniforms.h"
#include <core/str/str.h>

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

		core::SharedRef<IMeshletPipeline>      pipeline;
		core::str::unordered_str_map<Uniforms> uniforms;

		Uniforms&
		operator[](const std::string& cbuffer)
		{
			return uniforms.at(cbuffer);
		}

		[[nodiscard]]
		bool
		ContainsUniforms(std::string_view cbuffer) const
		{
			return uniforms.contains(cbuffer);
		}

		[[nodiscard]]
		Uniforms*
		FindUniforms(std::string_view cbuffer)
		{
			auto it = uniforms.find(cbuffer);
			if (it != uniforms.end())
			{
				return &it->second;
			}
			else
			{
				return nullptr;
			}
		}

		void
		Reset()
		{
			pipeline.Reset();
			uniforms.clear();
		}
	};
}
