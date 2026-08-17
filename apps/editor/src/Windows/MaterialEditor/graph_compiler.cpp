#include "graph_compiler.h"

#include "Render/Renderer.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

#include <QDebug>

#include <assetlib_structs/BMaterial.h>

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
}

namespace editor
{
	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph& graph,
		Renderer&                renderer,
		MaterialPreviewWindow&   preview)
	{
		const MaterialOutputNode* output = graph.model->OutputNode();
		if (output == nullptr)
			return;

		auto desc            = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor = output->BaseColorFactor();
		desc.metallicFactor  = output->MetallicFactor();
		desc.roughnessFactor = output->RoughnessFactor();

		desc.layerType          = ToLayerType(output->GetAlphaMode());
		desc.alphaCutoff        = output->GetAlphaCutoff();
		desc.transmissionFactor = output->GetTransmission();

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

		if (graph.preview.IsValid() && graph.preview.layerType == desc.layerType)
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
