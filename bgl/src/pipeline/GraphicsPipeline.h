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
	struct GraphicsPipelineDesc
	{
		ShaderHandle                                    vertexShader;
		ShaderHandle                                    pixelShader;
		RenderState                                     renderState;
		core::static_vector<Format, c_MaxRenderTargets> rtvFormats;
		Format                                          dsvFormat = Format::UNKNOWN;
		size_t                                          rootConstantsSize;

		GraphicsPipelineDesc&
		AddRtvFormat(const Rtv& rtv);

		GraphicsPipelineDesc&
		AddRtvFormat(const Format& fmt)
		{
			rtvFormats.push_back(fmt);
			return *this;
		}

		GraphicsPipelineDesc&
		SetDsvFormat(const Format& fmt)
		{
			dsvFormat = fmt;
			return *this;
		}
	};

	class IGraphicsPipeline : public core::Ref
	{};

	using GraphicsPipelineHandle = core::SharedRef<IGraphicsPipeline>;
}
