#pragma once
#include "pipeline/ComputePipeline.h"
#include "uniforms/Uniforms.h"
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <core/str/str.h>
#include <string_view>

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

		core::SharedRef<IComputePipeline>      pipeline;
		core::str::unordered_str_map<Uniforms> uniforms;

		Uniforms&
		operator[](std::string_view cbuffer)
		{
			const auto it = uniforms.find(cbuffer);
			if (it == uniforms.end())
				core::throw_runtime_error("the kernel declares no constant buffer '{}'", cbuffer);
			return it->second;
		}

		void
		Reset()
		{
			pipeline.Reset();
			uniforms.clear();
		}
	};
}
