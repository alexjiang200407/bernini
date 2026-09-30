#pragma once
#include <algorithm>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <core/str/str.h>
#include <span>
#include <string>
#include <string_view>

namespace bgpu
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
		operator[](std::string_view cbuffer)
		{
			const auto it = uniforms.find(cbuffer);
			if (it == uniforms.end())
				core::throw_runtime_error("the kernel declares no constant buffer '{}'", cbuffer);
			return it->second;
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

	[[nodiscard]] inline bool
	AnyInitialized(std::span<const MeshletKernel> kernels) noexcept
	{
		return std::ranges::any_of(kernels, [](const MeshletKernel& kernel) {
			return kernel.pipeline.IsInitialized();
		});
	}
}
