#pragma once
#include "constants/constants.h"
#include "resource/Rtv.h"
#include "resource/Shader.h"
#include "types/Format.h"
#include "types/RenderState.h"
#include <core/containers/static_vector.h>
#include <core/pimpl/RefCountPImpl.h>

namespace bgl
{
	struct GraphicsPipelineDesc
	{
		Shader                                          vertexShader;
		Shader                                          pixelShader;
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

	class GraphicsPipelineImpl;
	class GraphicsPipeline : public core::RefCountPImpl<GraphicsPipelineImpl>
	{
	public:
		GraphicsPipeline() = default;

		friend class DeviceImpl;
	};
}
