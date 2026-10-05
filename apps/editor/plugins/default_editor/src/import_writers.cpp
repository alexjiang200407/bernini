#include <assetlib/bmesh.h>
#include <assetlib/import_document.h>
#include <core/err/util.h>
#include <default_editor/import_writers.h>

#include "Windows/MeshEditor/MaterialGraphModel.h"
#include "Windows/MeshEditor/material_graph.h"
#include <assetlib_structs/BMaterialImport.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Mesh.h>

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/LanguageResolver.h>
#include <filesystem>
#include <gamelib/shading_model.h>
#include <qlogging.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
	namespace
	{
		// Where glTF's own base colour lands on a surface, by the names the shipped toon surfaces use.
		constexpr std::string_view c_BaseColorFactor = "baseColorFactor";
		constexpr std::string_view c_BaseColorSlot   = "baseColor";

		const bgl::SurfaceType*
		FindSurface(std::span<const bgl::SurfaceType> surfaces, std::string_view name)
		{
			const auto it = std::ranges::find(surfaces, name, &bgl::SurfaceType::surfaceName);
			return it == surfaces.end() ? nullptr : &*it;
		}

		void
		SetValue(assetlib::SurfaceParams& params, std::string_view field, std::vector<float> value)
		{
			const auto it =
				std::ranges::find(params.values, field, &assetlib::SurfaceValueBinding::name);
			if (it != params.values.end())
				it->value = std::move(value);
			else
				params.values.push_back({ std::string(field), std::move(value) });
		}

		void
		BindTexture(assetlib::SurfaceParams& params, std::string_view slot, std::string texture)
		{
			const auto it =
				std::ranges::find(params.textures, slot, &assetlib::SurfaceTextureBinding::name);
			if (it != params.textures.end())
			{
				it->texturePath = std::move(texture);
				return;
			}
			auto binding        = assetlib::SurfaceTextureBinding();
			binding.name        = std::string(slot);
			binding.texturePath = std::move(texture);
			params.textures.push_back(std::move(binding));
		}

		/**
		 * The document a material's extras describe on `surface`, in the order and by the rules
		 * docs/asset_standards.md § A surface named in a material's extras gives.
		 *
		 * @param textureKey The data-root-relative key of an imported texture index, empty for none.
		 */
		template <typename TextureKey>
		assetlib::BMaterial
		SurfaceDocument(
			const assetlib::imp::BMaterialImport& source,
			std::string_view                      materialName,
			const bgl::SurfaceType&               surface,
			const TextureKey&                     textureKey)
		{
			auto material                = assetlib::BMaterial();
			material.shadingModel        = game::ToShadingModel(surface.shading);
			material.layer.alphaMode     = source.alphaMode;
			material.layer.alphaCutoff   = source.alphaCutoff;
			material.layer.doubleSided   = source.doubleSided;
			material.surface.surfaceName = surface.surfaceName;

			const auto declaredValue = [&](std::string_view field) -> const bgl::SurfaceValue* {
				const auto it =
					std::ranges::find(surface.params.values, field, &bgl::SurfaceValue::name);
				return it == surface.params.values.end() ? nullptr : &*it;
			};
			const auto declaresTexture = [&](std::string_view slot) {
				return std::ranges::contains(
					surface.params.textures,
					slot,
					&bgl::SurfaceTexture::name);
			};

			if (const bgl::SurfaceValue* factor = declaredValue(c_BaseColorFactor);
			    factor != nullptr && factor->type == bgl::SurfaceValueType::kFloat4)
			{
				const glm::vec4& color = source.baseColorFactor;
				SetValue(
					material.surface,
					c_BaseColorFactor,
					{ color.r, color.g, color.b, color.a });
			}

			if (const std::string key = textureKey(source.baseColorTexture);
			    !key.empty() && declaresTexture(c_BaseColorSlot))
				BindTexture(material.surface, c_BaseColorSlot, key);

			for (const assetlib::imp::SurfaceValueImport& value : source.surface.values)
			{
				const bgl::SurfaceValue* declared = declaredValue(value.field);
				if (declared == nullptr ||
				    bgl::SurfaceValueComponents(declared->type) != value.width)
				{
					qWarning(
						"Import: material '%.*s' sets '%s' with %u number(s), which surface '%s' "
						"does not declare at that width; dropped",
						static_cast<int>(materialName.size()),
						materialName.data(),
						value.field.c_str(),
						value.width,
						surface.surfaceName.c_str());
					continue;
				}

				auto components = std::vector<float>(value.width);
				for (uint32_t c = 0; c < value.width; ++c)
					components[c] = value.value[static_cast<glm::length_t>(c)];
				SetValue(material.surface, value.field, std::move(components));
			}

			for (const assetlib::imp::SurfaceSlotImport& texture : source.surface.textures)
			{
				if (!declaresTexture(texture.field))
				{
					qWarning(
						"Import: material '%.*s' binds '%s', which surface '%s' does not declare; "
						"dropped",
						static_cast<int>(materialName.size()),
						materialName.data(),
						texture.field.c_str(),
						surface.surfaceName.c_str());
					continue;
				}

				const std::string key = textureKey(texture.texture);
				if (key.empty())
				{
					qWarning(
						"Import: material '%.*s' binds '%s' to an image this import extracted no "
						"texture for; dropped",
						static_cast<int>(materialName.size()),
						materialName.data(),
						texture.field.c_str());
					continue;
				}
				BindTexture(material.surface, texture.field, key);
			}

			return material;
		}
	}

	std::vector<assetlib::MaterialBinding>
	WriteImportedMaterials(
		const assetlib::imp::BMeshImport& imported,
		const assetlib::BMesh&            mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      materialDir,
		const std::filesystem::path&      textureDir,
		std::span<const QString>          stems,
		std::span<const bgl::SurfaceType> surfaces)
	{
		namespace fs = std::filesystem;

		// The stems were chosen against a material table probed before the dialog opened; this one comes
		// from a second parse of the same file after it closed. A source re-exported while the dialog sat
		// open has a different table, and stems taken from the old one would name files after materials
		// that are no longer at those indices.
		if (stems.size() != imported.materials.size())
		{
			core::throw_runtime_error(
				"this file's materials changed while the import dialog was open; import it again");
		}

		// No device: the graph is authored, not drawn, and a TextureNode takes a null scene on
		// purpose. No text is ever shown along this path, so an unregistered resolver -- which only
		// ever answers with the fallback it is given -- is all a node here needs.
		const editor::LanguageResolver language;
		const auto registry = MakeMaterialNodeRegistry(language, nullptr, nullptr, surfaces);

		const std::vector<std::string> textureNames = assetlib::importedTextureFileNames(imported);

		const auto texturePath = [&](uint32_t index) {
			return index >= textureNames.size() ?
			           QString() :
			           QString::fromStdWString((textureDir / textureNames[index]).wstring());
		};

		const auto textureKey = [&](uint32_t index) {
			const QString path = texturePath(index);
			return path.isEmpty() ? std::string() : Rebase(path, dataRoot, true).toStdString();
		};

		auto relative = std::vector<std::string>(imported.materials.size());

		for (size_t i = 0; i < imported.materials.size(); ++i)
		{
			const assetlib::imp::BMaterialImport& source = imported.materials[i];
			const std::string_view materialName = imported.stringPool.at(source.nameOffset);

			const std::string&      surfaceName = source.surface.surfaceName;
			const bgl::SurfaceType* surface     = FindSurface(surfaces, surfaceName);
			if (!surfaceName.empty() && surface == nullptr)
				qWarning(
					"Import: material '%.*s' names surface '%s', which this project does not "
					"register; %s",
					static_cast<int>(materialName.size()),
					materialName.data(),
					surfaceName.c_str(),
					source.isPbr ? "imported as PBR" : "left behind");

			// A material whose shading model the engine has no payload for is left behind rather than
			// stamped into a PBR one it never was, and carries no stem to be written under.
			if ((surface == nullptr && !source.isPbr) || stems[i].isEmpty())
				continue;

			const QString& stem = stems[i];
			const fs::path file = materialDir / (stem + ".bmaterial").toStdWString();

			MaterialGraphModel model(registry);
			if (surface != nullptr)
			{
				const assetlib::BMaterial document =
					SurfaceDocument(source, materialName, *surface, textureKey);
				if (!BuildSurfaceMaterialGraph(model, document, dataRoot))
					core::throw_runtime_error(
						"material '{}': surface '{}' has no sink to build",
						materialName,
						surfaceName);
			}
			else
			{
				BuildImportedMaterialGraph(
					model,
					source,
					ImportedMaterialMaps{ texturePath(source.baseColorTexture),
				                          texturePath(source.normalTexture),
				                          texturePath(source.ormTexture),
				                          texturePath(source.occlusionTexture),
				                          texturePath(source.geometryOcclusionTexture) });
			}

			const assetlib::AssetStore store(dataRoot);
			store.Save(CompileMaterial(model, stem, dataRoot), store.KeyFor(file));

			relative[i] =
				Rebase(QString::fromStdWString(file.wstring()), dataRoot, true).toStdString();
		}

		// A binding names every level of a submesh at once, so only level 0 is bound.
		auto bindings = std::vector<assetlib::MaterialBinding>();
		for (const assetlib::Submesh& submesh : assetlib::lodNSubmeshes(mesh, 0))
		{
			const uint32_t index = submesh.material;
			if (index >= relative.size() || relative[index].empty())
				continue;

			bindings.push_back(
				{ std::string(mesh.stringPool.at(submesh.nameOffset)), relative[index] });
		}
		return bindings;
	}
}
