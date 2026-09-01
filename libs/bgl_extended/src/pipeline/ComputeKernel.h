#pragma once
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	struct ComputeKernel
	{
		ComputeKernel()                         = default;
		ComputeKernel(const ComputeKernel&)     = delete;
		ComputeKernel(ComputeKernel&&) noexcept = default;

		ComputeKernel&
		operator=(const ComputeKernel&) = delete;

		ComputeKernel&
		operator=(ComputeKernel&&) noexcept = default;

		core::SharedRef<IComputePipeline>         pipeline;
		std::unordered_map<std::string, Uniforms> uniforms;

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
