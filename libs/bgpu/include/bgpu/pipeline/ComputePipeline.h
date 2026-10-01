#pragma once
#include <bgpu/resource/Shader.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>

#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	class IShader;

	struct ComputePipelineDesc
	{
		core::SharedRef<IShader> shader = nullptr;
		std::string              debugName;

		template <typename Self>
		Self&&
		SetShader(this Self&& self, core::SharedRef<IShader> value)
		{
			self.shader = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value)
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
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
		GetUniformLayoutEntry(std::string_view name) const noexcept = 0;

		virtual std::vector<std::string>
		GetUniformBufferNames() const noexcept = 0;
	};

	using ComputePipelineRef = core::SharedRef<IComputePipeline>;
}
