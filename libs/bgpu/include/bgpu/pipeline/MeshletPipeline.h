#pragma once
#include <bgpu/constants/constants.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/RenderState.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>

#include <core/containers/static_vector.h>
#include <core/ref/SharedRef.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	class IShader;

	struct MeshletPipelineDesc
	{
		core::SharedRef<IShader>                        ampShader   = nullptr;
		core::SharedRef<IShader>                        meshShader  = nullptr;
		core::SharedRef<IShader>                        pixelShader = nullptr;
		RenderState                                     renderState;
		core::static_vector<Format, c_MaxRenderTargets> rtvFormats;
		Format                                          dsvFormat = Format::UNKNOWN;

		template <typename Self>
		Self&&
		SetAmplificationShader(this Self&& self, core::SharedRef<IShader> shader)
		{
			self.ampShader = std::move(shader);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMeshShader(this Self&& self, core::SharedRef<IShader> shader)
		{
			self.meshShader = std::move(shader);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPixelShader(this Self&& self, core::SharedRef<IShader> shader)
		{
			self.pixelShader = std::move(shader);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddRtvFormat(this Self&& self, const Format& fmt)
		{
			self.rtvFormats.push_back(fmt);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDsvFormat(this Self&& self, const Format& fmt)
		{
			self.dsvFormat = fmt;
			return std::forward<Self>(self);
		}
	};

	class IMeshletPipeline : public core::Ref
	{
	public:
		IMeshletPipeline() noexcept                        = default;
		IMeshletPipeline(const IMeshletPipeline&) noexcept = delete;
		IMeshletPipeline(IMeshletPipeline&&) noexcept      = delete;

		IMeshletPipeline&
		operator=(const IMeshletPipeline&) noexcept = delete;

		IMeshletPipeline&
		operator=(IMeshletPipeline&&) noexcept = delete;

		virtual const MeshletPipelineDesc&
		GetDesc() const noexcept = 0;

		virtual UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept = 0;

		// Names of every constant buffer the shader declares (empty if it has none).
		virtual std::vector<std::string>
		GetUniformBufferNames() const noexcept = 0;
	};

	using MeshletPipelineRef = core::SharedRef<IMeshletPipeline>;
}
