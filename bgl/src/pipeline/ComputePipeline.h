#pragma once
#include "resource/Shader.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class IShader;

	struct ComputePipelineDesc
	{
		core::SharedRef<IShader> shader = nullptr;
		std::string              debugName;

		ComputePipelineDesc&
		SetShader(core::SharedRef<IShader> _shader)
		{
			shader = std::move(_shader);
			return *this;
		}

		ComputePipelineDesc&
		SetDebugName(std::string _debugName)
		{
			debugName = std::move(_debugName);
			return *this;
		}
	};

	class IComputePipeline : public core::Ref
	{
	public:
		IComputePipeline() noexcept                        = default;
		IComputePipeline(const IComputePipeline&) noexcept = delete;
		IComputePipeline(IComputePipeline&&) noexcept      = delete;

		IComputePipeline&
		operator=(const IComputePipeline&) noexcept = delete;

		IComputePipeline&
		operator=(IComputePipeline&&) noexcept = delete;

		virtual const ComputePipelineDesc&
		GetDesc() const noexcept = 0;

		virtual UniformLayoutEntry
		GetUniformLayoutEntry(const std::string& name) const noexcept = 0;
	};

	using ComputePipelineHandle = core::SharedRef<IComputePipeline>;
}
