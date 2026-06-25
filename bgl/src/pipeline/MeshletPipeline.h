#pragma once
#include "constants/constants.h"
#include "resource/Rtv.h"
#include "resource/Shader.h"
#include "types/Format.h"
#include "types/RenderState.h"

#include <core/containers/static_vector.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct MeshletPipelineDesc
	{
		ShaderHandle                                    ampShader   = nullptr;
		ShaderHandle                                    meshShader  = nullptr;
		ShaderHandle                                    pixelShader = nullptr;
		RenderState                                     renderState;
		core::static_vector<Format, c_MaxRenderTargets> rtvFormats;
		Format                                          dsvFormat = Format::UNKNOWN;

		MeshletPipelineDesc&
		SetAmplificationShader(ShaderHandle shader)
		{
			ampShader = std::move(shader);
			return *this;
		}

		MeshletPipelineDesc&
		SetMeshShader(ShaderHandle shader)
		{
			meshShader = std::move(shader);
			return *this;
		}

		MeshletPipelineDesc&
		SetPixelShader(ShaderHandle shader)
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

	struct UniformLayoutEntry
	{
		uint32_t                     size;
		slang::TypeLayoutReflection* layout;
		uint32_t                     rootParamIndex = 0xFFFFFFFF;
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
	};

	using MeshletPipelineHandle = core::SharedRef<IMeshletPipeline>;
}
