#pragma once
#include "pipeline/GraphicsPipeline.h"
#include "uniforms/Uniforms.h"
#include <core/str/str.h>

namespace bgl
{
	// A graphics pipeline paired with the uniforms for every constant buffer it declares
	// (keyed by buffer name). Created by IDevice::CreateGraphicsKernel. The map is empty for
	// a shader with no constant buffers.
	struct GraphicsKernel
	{
		GraphicsKernel()                          = default;
		GraphicsKernel(const GraphicsKernel&)     = delete;
		GraphicsKernel(GraphicsKernel&&) noexcept = default;

		GraphicsKernel&
		operator=(const GraphicsKernel&) = delete;

		GraphicsKernel&
		operator=(GraphicsKernel&&) noexcept = default;

		core::SharedRef<IGraphicsPipeline>     pipeline;
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
			return it != uniforms.end() ? &it->second : nullptr;
		}

		void
		Reset()
		{
			pipeline.Reset();
			uniforms.clear();
		}
	};
}
