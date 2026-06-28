#pragma once
#include "constants/constants.h"
#include "resource/Rtv.h"
#include "resource/Shader.h"
#include "types/Format.h"
#include "types/RenderState.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/containers/static_vector.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
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

		MeshletPipelineDesc&
		SetAmplificationShader(core::SharedRef<IShader> shader)
		{
			ampShader = std::move(shader);
			return *this;
		}

		MeshletPipelineDesc&
		SetMeshShader(core::SharedRef<IShader> shader)
		{
			meshShader = std::move(shader);
			return *this;
		}

		MeshletPipelineDesc&
		SetPixelShader(core::SharedRef<IShader> shader)
		{
			pixelShader = std::move(shader);
			return *this;
		}

		MeshletPipelineDesc&
		AddRtvFormat(const Rtv& rtv);

		MeshletPipelineDesc&
		AddRtvFormat(const Format& fmt)
		{
			rtvFormats.push_back(fmt);
			return *this;
		}

		MeshletPipelineDesc&
		SetDsvFormat(const Format& fmt)
		{
			dsvFormat = fmt;
			return *this;
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
		GetUniformLayoutEntry(const std::string& name) const noexcept = 0;

		// Names of every constant buffer the shader declares (empty if it has none).
		virtual std::vector<std::string>
		GetUniformBufferNames() const noexcept = 0;
	};

	using MeshletPipelineHandle = core::SharedRef<IMeshletPipeline>;
}
