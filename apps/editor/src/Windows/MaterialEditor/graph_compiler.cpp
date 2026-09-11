#include "graph_compiler.h"

#include "Render/Renderer.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include <bgl/LayerType.h>
#include <bgl/SurfaceType.h>

#include <QDebug>
#include <qobject.h>

#include <assetlib_structs/BMaterial.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <qlogging.h>

namespace
{
	bgl::LayerType
	ToLayerType(assetlib::AlphaMode mode) noexcept
	{
		switch (mode)
		{
		case assetlib::AlphaMode::kMask:
			return bgl::LayerType::kMask;
		case assetlib::AlphaMode::kBlend:
			return bgl::LayerType::kBlend;
		case assetlib::AlphaMode::kHashed:
			return bgl::LayerType::kHashed;
		case assetlib::AlphaMode::kOpaque:
			break;
		}
		return bgl::LayerType::kOpaque;
	}

	void
	BindSurfacePreview(
		MaterialGraphSet::Graph&        graph,
		Renderer&                       renderer,
		MaterialPreviewWindow&          preview,
		bgl::MaterialType               kind,
		const bgl::SurfaceMaterialDesc& desc)
	{
		// An update keeps the record's surface and layer, so it stands only while both still
		// agree -- `kind` is *this* surface's, so a handle from another surface, or from a PBR
		// board this one replaced, is a different PSO row and has to be a new material.
		if (graph.preview.IsValid() && graph.preview.materialType == kind &&
		    graph.preview.layerType == desc.layerType)
		{
			renderer.Post([owner = &renderer, handle = graph.preview, desc] {
				try
				{
					owner->GetScene()->UpdateSurfaceMaterial(handle, desc);
				}
				catch (const std::exception& e)
				{
					qWarning("MaterialEditor: could not update a surface preview: %s", e.what());
				}
			});
			return;
		}

		const bgl::MaterialHandle previous = graph.preview;

		// A surface the engine never registered, or a parameter the document invented, is refused
		// here rather than on the render thread. Leaving the previous binding is the readable
		// failure: the viewport keeps drawing what it last drew and the warning says why.
		try
		{
			graph.preview =
				renderer.Invoke([&] { return renderer.GetScene()->CreateSurfaceMaterial(desc); });
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialEditor: could not create a surface preview: %s", e.what());
			return;
		}

		for (const uint32_t submesh : graph.submeshes)
			preview.SetSubmeshMaterial(submesh, graph.preview);

		if (previous.IsValid())
		{
			renderer.Post([owner = &renderer, previous] {
				try
				{
					owner->GetScene()->DeleteMaterial(previous);
				}
				catch (const std::exception& e)
				{
					qWarning("MaterialEditor: could not delete a preview material: %s", e.what());
				}
			});
		}
	}
}

namespace editor
{
	bgl::SurfaceMaterialDesc
	SurfaceDescOfBoard(const SurfaceOutputNode& sink)
	{
		const bgl::SurfaceType& surface = sink.Surface();

		auto desc        = bgl::SurfaceMaterialDesc();
		desc.surface     = surface.name;
		desc.layerType   = ToLayerType(sink.GetAlphaMode());
		desc.alphaCutoff = sink.GetAlphaCutoff();
		desc.doubleSided = sink.GetDoubleSided();

		// Every declared value at its current setting, carried at four wide; the renderer is the
		// side that knows how many components the parameter was declared with.
		desc.values.reserve(surface.params.values.size());
		for (size_t i = 0; i < surface.params.values.size(); ++i)
			desc.values.emplace_back(surface.params.values[i].name, sink.Value(i));

		for (size_t slot = 0; slot < surface.params.textures.size(); ++slot)
		{
			if (sink.BoundTexture(slot).isEmpty())
				continue;
			desc.textures.emplace_back(
				surface.params.textures[slot].name,
				sink.BoundTextureAsset(slot));
		}

		return desc;
	}

	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph& graph,
		Renderer&                renderer,
		MaterialPreviewWindow&   preview)
	{
		if (const auto* surface = qobject_cast<const SurfaceOutputNode*>(graph.model->OutputNode()))
		{
			BindSurfacePreview(
				graph,
				renderer,
				preview,
				surface->Surface().kind,
				SurfaceDescOfBoard(*surface));
			return;
		}

		const auto* output = qobject_cast<const MaterialOutputNode*>(graph.model->OutputNode());
		if (output == nullptr)
			return;

		auto desc            = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor = output->BaseColorFactor();
		desc.metallicFactor  = output->MetallicFactor();
		desc.roughnessFactor = output->RoughnessFactor();

		desc.layerType          = ToLayerType(output->GetAlphaMode());
		desc.alphaCutoff        = output->GetAlphaCutoff();
		desc.doubleSided        = output->GetDoubleSided();
		desc.transmissionFactor = output->GetTransmission();

		desc.specularColorFactor = output->GetSpecularColorFactor();
		desc.specularFactor      = output->GetSpecularFactor();

		const auto route = [&](unsigned int channel) {
			const ChannelData::Route wired = output->Route(channel);

			auto out    = bgl::ChannelRouteDesc();
			out.texture = wired.texture;
			out.channel = wired.channel;
			return out;
		};

		// The channel runs come from BMaterial.h, which owns the `routes` array a graph is saved into.
		// A literal offset here would silently disagree with the baker the moment a channel is added.
		const auto channel = [](const assetlib::ChannelGroup& group, size_t component) {
			return static_cast<unsigned int>(assetlib::channelIndex(group, component));
		};

		for (size_t i = 0; i < desc.baseColor.size(); ++i)
			desc.baseColor[i] = route(channel(assetlib::c_BaseColorChannels, i));
		for (size_t i = 0; i < desc.orm.size(); ++i)
			desc.orm[i] = route(channel(assetlib::c_OrmChannels, i));
		for (size_t i = 0; i < desc.normal.size(); ++i)
			desc.normal[i] = route(channel(assetlib::c_NormalChannels, i));

		// The kind check matters since a board switches: a handle left by a surface sink is a
		// different PSO row, and updating it in place would throw on every keystroke.
		if (graph.preview.IsValid() && graph.preview.materialType == bgl::MaterialType::kLoosePbr &&
		    graph.preview.layerType == desc.layerType)
		{
			// Fire-and-forget on every keystroke; the instances already override with this handle, so the
			// in-place rewrite is all the edit needs.
			renderer.Post([owner = &renderer, handle = graph.preview, desc] {
				try
				{
					owner->GetScene()->UpdateLoosePbrMaterial(handle, desc);
				}
				catch (const std::exception& e)
				{
					qWarning("MaterialEditor: could not update a preview material: %s", e.what());
				}
			});
			return;
		}

		const bgl::MaterialHandle previous = graph.preview;

		// Bind the replacement before destroying what it replaces: a deleted material leaves its slot to
		// be reused, and an instance still overriding with it would silently wear whatever lands there. The
		// override (SetSubmeshMaterial) and the delete are both posted, so they run in that order.
		graph.preview =
			renderer.Invoke([&] { return renderer.GetScene()->CreateLoosePbrMaterial(desc); });
		for (const uint32_t submesh : graph.submeshes)
			preview.SetSubmeshMaterial(submesh, graph.preview);

		if (previous.IsValid())
		{
			renderer.Post([owner = &renderer, previous] {
				try
				{
					owner->GetScene()->DeleteMaterial(previous);
				}
				catch (const std::exception& e)
				{
					qWarning("MaterialEditor: could not delete a preview material: %s", e.what());
				}
			});
		}
	}
}
