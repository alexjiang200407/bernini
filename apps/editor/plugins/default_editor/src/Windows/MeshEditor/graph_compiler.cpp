#include "graph_compiler.h"
#include <assetlib/bmaterial.h>

#include "Windows/MeshEditor/MaterialGraphModel.h"
#include "Windows/MeshEditor/MaterialGraphSet.h"
#include "Windows/MeshEditor/MeshPreviewWindow.h"
#include "Windows/MeshEditor/nodes/ChannelData.h"
#include "Windows/MeshEditor/nodes/MaterialOutputNode.h"
#include "Windows/MeshEditor/nodes/SurfaceOutputNode.h"
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <bgl/SurfaceType.h>
#include <bgl/types/LayerType.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>

#include <QDebug>
#include <qobject.h>

#include "Windows/MeshEditor/material_graph.h"
#include <assetlib_structs/BMaterial.h>
#include <bgl/IScene.h>
#include <bgl/MaterialType.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/TextureAssetHandle.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <qchar.h>
#include <qlogging.h>
#include <qstring.h>
#include <span>
#include <utility>
#include <vector>

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

	// What one graph's sink compiles to, built on the GUI thread for the render thread to apply.
	struct PreviewPlan
	{
		MaterialGraphSet::Graph* graph = nullptr;

		// One of the two, by the sink's kind; `surfaceKind` is the surface's bucket.
		std::optional<bgl::LoosePbrMaterialDesc> pbr;
		std::optional<bgl::SurfaceMaterialDesc>  surface;
		bgl::MaterialType                        surfaceKind = bgl::MaterialType::kLoosePbr;

		bgl::MaterialHandle previous;  // replaced by a new material, deleted once that is bound
		bool                created = false;
	};

	[[nodiscard]] bgl::LoosePbrMaterialDesc
	PbrDescOfBoard(const MaterialOutputNode& output)
	{
		auto desc            = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor = output.BaseColorFactor();
		desc.metallicFactor  = output.MetallicFactor();
		desc.roughnessFactor = output.RoughnessFactor();

		desc.layerType          = ToLayerType(output.GetAlphaMode());
		desc.alphaCutoff        = output.GetAlphaCutoff();
		desc.doubleSided        = output.GetDoubleSided();
		desc.transmissionFactor = output.GetTransmission();

		desc.specularColorFactor = output.GetSpecularColorFactor();
		desc.specularFactor      = output.GetSpecularFactor();

		const auto route = [&](unsigned int channel) {
			const ChannelData::Route wired = output.Route(channel);

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

		desc.geometryOcclusionTexture = output.GeometryOcclusionRoute().texture;
		return desc;
	}

	// Rewrites the graph's material in place where its bucket allows, else creates a new one. Render
	// thread. The kind check matters since a board switches: a handle left by the other kind of sink
	// is a different bucket, and updating it in place would throw on every keystroke.
	void
	ApplyPlan(PreviewPlan& plan, bgl::IScene& scene)
	{
		const bgl::MaterialHandle current = plan.graph->preview;

		if (plan.pbr.has_value())
		{
			if (current.IsValid() && current.materialType == bgl::MaterialType::kLoosePbr &&
			    current.layerType == plan.pbr->layerType)
			{
				scene.UpdateLoosePbrMaterial(current, *plan.pbr);
				return;
			}

			plan.graph->preview = scene.CreateLoosePbrMaterial(*plan.pbr);
		}
		else
		{
			// An update keeps the record's surface and layer, so it stands only while both still
			// agree -- a handle from another surface, or from a PBR board this one replaced, is a
			// different bucket and has to be a new material.
			if (current.IsValid() && current.materialType == plan.surfaceKind &&
			    current.layerType == plan.surface->layerType)
			{
				scene.UpdateSurfaceMaterial(current, *plan.surface);
				return;
			}

			plan.graph->preview = scene.CreateSurfaceMaterial(*plan.surface);
		}

		plan.previous = current;
		plan.created  = true;
	}
}

namespace editor
{
	bgl::SurfaceMaterialDesc
	SurfaceDescOfBoard(const SurfaceOutputNode& sink)
	{
		const bgl::SurfaceType& surface = sink.Surface();

		auto desc        = bgl::SurfaceMaterialDesc();
		desc.surfaceName = surface.surfaceName;
		desc.layerType   = ToLayerType(sink.GetAlphaMode());
		desc.alphaCutoff = sink.GetAlphaCutoff();
		desc.doubleSided = sink.GetDoubleSided();

		// Every declared value at its current setting, carried at four wide; the host is the
		// side that knows how many components the parameter was declared with.
		desc.values.reserve(surface.params.values.size());
		for (size_t i = 0; i < surface.params.values.size(); ++i)
			desc.values.emplace_back(surface.params.values[i].name, sink.Value(i));

		for (size_t slot = 0; slot < surface.params.textures.size(); ++slot)
		{
			// The wires are the routes and their Texture nodes own the uploads, so a routed
			// slot's desc is built from what is already on the board -- nothing is composited
			// anywhere, which is what makes a rewire cost a record write.
			if (sink.SlotIsRouted(slot))
			{
				auto binding = bgl::SurfaceTextureBinding();
				binding.name = surface.params.textures[slot].name;
				for (uint32_t c = 0; c < assetlib::c_SurfaceSlotChannelCount; ++c)
				{
					const ChannelData::Route route = sink.RouteFor(slot, c);
					if (route.path.isEmpty())
						continue;
					binding.routes[c].texture = route.texture;
					binding.routes[c].channel = route.channel;
				}
				desc.textures.push_back(std::move(binding));
				continue;
			}

			if (sink.BoundTexture(slot).isEmpty())
				continue;
			desc.textures.emplace_back(
				surface.params.textures[slot].name,
				sink.BoundTextureAsset(slot));
		}

		return desc;
	}

	void
	CompilePreviewMaterials(
		std::span<MaterialGraphSet::Graph* const> graphs,
		editor::IEditorHost&                      host,
		MeshPreviewWindow&                        preview)
	{
		auto plans = std::vector<PreviewPlan>();
		plans.reserve(graphs.size());
		for (MaterialGraphSet::Graph* graph : graphs)
		{
			auto plan  = PreviewPlan();
			plan.graph = graph;

			const QtNodes::NodeDelegateModel* sink = graph->model->OutputNode();
			if (const auto* surface = qobject_cast<const SurfaceOutputNode*>(sink))
			{
				plan.surface     = SurfaceDescOfBoard(*surface);
				plan.surfaceKind = surface->Surface().kind;
			}
			else if (const auto* output = qobject_cast<const MaterialOutputNode*>(sink))
			{
				plan.pbr = PbrDescOfBoard(*output);
			}
			else
			{
				continue;
			}
			plans.push_back(std::move(plan));
		}
		if (plans.empty())
			return;

		// One round trip for every graph. A surface the engine never registered, or a parameter
		// the document invented, is refused here rather than on the render thread; leaving that
		// graph's previous binding is the readable failure: the viewport keeps drawing what it last
		// drew and the warning says why.
		host.InvokeRender([&](editor::RenderContext& context) {
			for (PreviewPlan& plan : plans)
			{
				try
				{
					ApplyPlan(plan, context.scene);
				}
				catch (const std::exception& e)
				{
					qWarning("MeshEditor: could not compile a preview material: %s", e.what());
				}
			}
		});

		// Bind the replacements before destroying what they replace: a deleted material leaves its
		// slot to be reused, and an instance still overriding with it would silently wear whatever
		// lands there.
		auto bindings = std::vector<MeshPreviewWindow::SubmeshMaterial>();
		auto replaced = std::vector<bgl::MaterialHandle>();
		for (const PreviewPlan& plan : plans)
		{
			if (!plan.created)
				continue;
			for (const uint32_t submesh : plan.graph->submeshes)
				bindings.push_back({ submesh, plan.graph->preview });
			if (plan.previous.IsValid())
				replaced.push_back(plan.previous);
		}
		if (!bindings.empty())
			preview.SetSubmeshMaterials(bindings);

		if (replaced.empty())
			return;

		host.InvokeRender([&replaced](editor::RenderContext& context) {
			for (const bgl::MaterialHandle& material : replaced)
			{
				try
				{
					context.scene.DeleteMaterial(material);
				}
				catch (const std::exception& e)
				{
					qWarning("MeshEditor: could not delete a preview material: %s", e.what());
				}
			}
		});
	}

	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph& graph,
		editor::IEditorHost&     host,
		MeshPreviewWindow&       preview)
	{
		MaterialGraphSet::Graph* const one = &graph;
		CompilePreviewMaterials({ &one, 1 }, host, preview);
	}
}
