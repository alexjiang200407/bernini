#pragma once
#include "constants/constants.h"
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

	/**
	 * The vertex + pixel stages, render state, and attachment formats of a traditional graphics
	 * pipeline. The two stages may come from different Slang modules, which are linked into one
	 * program.
	 */
	struct GraphicsPipelineDesc
	{
		core::SharedRef<IShader>                        vertexShader = nullptr;
		core::SharedRef<IShader>                        pixelShader  = nullptr;
		std::string                                     vertexEntry  = "vs_main";
		std::string                                     pixelEntry   = "fs_main";
		RenderState                                     renderState;
		core::static_vector<Format, c_MaxRenderTargets> rtvFormats;
		Format                                          dsvFormat = Format::UNKNOWN;
		std::string                                     debugName;

		GraphicsPipelineDesc&
		SetVertexShader(core::SharedRef<IShader> shader)
		{
			vertexShader = std::move(shader);
			return *this;
		}

		GraphicsPipelineDesc&
		SetPixelShader(core::SharedRef<IShader> shader)
		{
			pixelShader = std::move(shader);
			return *this;
		}

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

		GraphicsPipelineDesc&
		SetDebugName(std::string name)
		{
			debugName = std::move(name);
			return *this;
		}
	};

	/**
	 * A vertex->pixel pipeline, the raster seam for geometry that is not meshlet-partitioned: the
	 * skybox, fullscreen passes, and anything else drawn from a vertex count rather than a meshlet
	 * dispatch. Unlike IMeshletPipeline, both backends implement this natively -- nothing is
	 * emulated -- which is why a shader on this path needs no per-target arm.
	 */
	class IGraphicsPipeline : public core::Ref
	{
	public:
		IGraphicsPipeline() noexcept                         = default;
		IGraphicsPipeline(const IGraphicsPipeline&) noexcept = delete;
		IGraphicsPipeline(IGraphicsPipeline&&) noexcept      = delete;

		IGraphicsPipeline&
		operator=(const IGraphicsPipeline&) noexcept = delete;

		IGraphicsPipeline&
		operator=(IGraphicsPipeline&&) noexcept = delete;

		virtual const GraphicsPipelineDesc&
		GetDesc() const noexcept = 0;

		virtual UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept = 0;

		// Names of every constant buffer the shader declares (empty if it has none).
		virtual std::vector<std::string>
		GetUniformBufferNames() const noexcept = 0;
	};

	using GraphicsPipelineRef = core::SharedRef<IGraphicsPipeline>;
}
