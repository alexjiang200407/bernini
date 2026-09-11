#include "graph_compiler.h"
#include <assetlib/bmaterial.h>

#include "Render/Renderer.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include <bgl/LayerType.h>

#include <QDebug>
#include <qobject.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BMaterial.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/glm.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <qlogging.h>
#include <string>
#include <utility>

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

	bool
	IsSurfaceMaterial(bgl::MaterialHandle handle) noexcept
	{
		const auto kind = static_cast<uint32_t>(handle.materialType);
		return handle.IsValid() && kind >= static_cast<uint32_t>(bgl::MaterialType::kGameStart) &&
		       kind < static_cast<uint32_t>(bgl::MaterialType::kCount);
	}

	void
	BindSurfacePreview(
		MaterialGraphSet::Graph&     graph,
		Renderer&                    renderer,
		MaterialPreviewWindow&       preview,
		const assetlib::BMaterial&   material,
		const std::filesystem::path& dataRoot)
	{
		// A texture that will not load is left null rather than abandoning the material: the surface
		// samples a default for it, which draws something an author can see is wrong -- where losing
		// the material would leave the last edit on screen and look like nothing happened.
		const bgl::SurfaceMaterialDesc desc =
			editor::SurfaceDescOf(material, [&](const std::string& key) {
				try
				{
					auto image = assetlib::loadKTX2(dataRoot / key);
					return renderer.Invoke(
						[&] { return renderer.GetScene()->AddTextureAsset(std::move(image)); });
				}
				catch (const std::exception& e)
				{
					qWarning(
						"MaterialEditor: could not load '%s' for a surface: %s",
						key.c_str(),
						e.what());
					return bgl::TextureAssetHandle();
				}
			});

		// An update keeps the record's surface and layer, so it stands only while both still agree;
		// anything else is a different PSO row and has to be a new material.
		if (IsSurfaceMaterial(graph.preview) && graph.preview.layerType == desc.layerType)
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
	SurfaceDescOf(const assetlib::BMaterial& material, const TextureLoader& loadTexture)
	{
		auto desc        = bgl::SurfaceMaterialDesc();
		desc.surface     = material.surface.name;
		desc.layerType   = ToLayerType(material.layer.alphaMode);
		desc.alphaCutoff = material.layer.alphaCutoff;
		desc.doubleSided = material.layer.doubleSided;

		desc.values.reserve(material.surface.values.size());
		for (const assetlib::SurfaceValueBinding& value : material.surface.values)
		{
			// Widened to four and narrowed again by the renderer, which is the only side that knows
			// how many components the parameter was declared with.
			auto binding = bgl::SurfaceValueBinding{ value.name, glm::vec4(0.0f) };
			for (size_t i = 0; i < value.value.size() && i < 4; ++i)
				binding.value[static_cast<glm::length_t>(i)] = value.value[i];
			desc.values.push_back(std::move(binding));
		}

		desc.textures.reserve(material.surface.textures.size());
		for (const assetlib::SurfaceTextureBinding& texture : material.surface.textures)
			desc.textures.emplace_back(texture.name, loadTexture(texture.texture));

		return desc;
	}

	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph&     graph,
		Renderer&                    renderer,
		MaterialPreviewWindow&       preview,
		const assetlib::BMaterial*   onDisk,
		const std::filesystem::path& dataRoot)
	{
		// A surface material previews from its document rather than from the live board: a Save
		// rewrites the document, and the next board change is what re-reads it here.
		if (onDisk != nullptr && onDisk->shadingModel == assetlib::ShadingModel::kPbrSurface)
		{
			BindSurfacePreview(graph, renderer, preview, *onDisk, dataRoot);
			return;
		}

		// The PBR preview reads the PBR sink's factors and routes; a sink of another kind has no
		// preview path here yet.
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
