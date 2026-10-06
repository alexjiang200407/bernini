#include "scene/Scene.h"
#include "scene/terrain_lod.h"
#include "util/util.h"
#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/IScene.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/glm.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/Terrain.h>
#include <bgl/idl/TerrainNodeBounds.h>
#include <bgl/types/LayerType.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <bgl/types/TextureAssetHandle.h>
#include <cmath>
#include <core/containers/fixed_buffer.h>
#include <core/containers/multi_slot_handle.h>
#include <core/err/util.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_InitialTerrains = 1;

		// One number declared twice, because a public header cannot reach the generated IDL.
		static_assert(c_MaxTerrainSamples == idl::cTerrainMaxSamples);

		// A patch's mesh group emits a (quads + 1)^2 grid, within the limits every mesh stage keeps.
		static_assert(
			idl::cTerrainPatchVertices ==
			(idl::cTerrainPatchQuads + 1) * (idl::cTerrainPatchQuads + 1));
		static_assert(
			idl::cTerrainPatchPrims == 2 * idl::cTerrainPatchQuads * idl::cTerrainPatchQuads);
		static_assert(idl::cTerrainPatchVertices <= idl::cMaxVerticesPerMeshlet);
		static_assert(idl::cTerrainPatchPrims <= idl::cMaxPrimsPerMeshlet);

		// The coarsest level's one node covers the widest heightfield allowed.
		static_assert(
			static_cast<uint64_t>(idl::cTerrainPatchQuads) << (idl::cTerrainMaxLevels - 1) >=
			idl::cTerrainMaxSamples - 1);

		[[nodiscard]] bool
		IsPositive(const float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		void
		ValidateTerrain(const TerrainDesc& desc, const std::span<const SurfaceType> surfaces)
		{
			const auto refuse = [](const std::string_view why) {
				throw SceneError(std::format("CreateTerrain: {}", why));
			};

			if (desc.heightfield == nullptr)
			{
				refuse("the heightfield is null");
			}
			const assetlib::Heightfield& field = *desc.heightfield;

			if (field.samplesX < 2 || field.samplesZ < 2 || field.samplesX > c_MaxTerrainSamples ||
			    field.samplesZ > c_MaxTerrainSamples)
			{
				refuse(
					std::format(
						"the heightfield is {} x {} samples; each axis takes 2 to {}",
						field.samplesX,
						field.samplesZ,
						c_MaxTerrainSamples));
			}
			if (field.heights.size() !=
			    static_cast<size_t>(field.samplesX) * static_cast<size_t>(field.samplesZ))
			{
				refuse(
					std::format(
						"the heightfield holds {} samples where {} x {} are declared",
						field.heights.size(),
						field.samplesX,
						field.samplesZ));
			}
			if (!IsPositive(field.cellSize) || !IsPositive(field.heightRange))
			{
				refuse("cellSize and heightRange must be finite and positive");
			}
			if (!std::isfinite(field.minHeight) || !core::is_finite(desc.origin))
			{
				refuse("minHeight and origin must be finite");
			}
			if (!IsPositive(desc.pixelsPerCell))
			{
				refuse("pixelsPerCell must be finite and positive");
			}

			const MaterialType kind = desc.material.materialType;
			if (!desc.material.IsValid() || kind == MaterialType::kNull ||
			    kind == MaterialType::kAssert)
			{
				refuse("the material must be a drawable one (not null, kNull or kAssert)");
			}
			if (desc.material.layerType != LayerType::kOpaque)
			{
				refuse("the material must be in the opaque layer");
			}
			// A character's programs read a placement's toon shading rig off its vertices; a
			// terrain patch carries none, so its pixels have nothing to read.
			if (const auto slot = GameSlot(kind);
			    slot.has_value() && *slot < surfaces.size() &&
			    surfaces[*slot].shading == SurfaceShading::kToonCharacter)
			{
				refuse(
					"a toon character surface shades a placement's rig, which a terrain has none "
					"of");
			}
		}

		/** The samples as one R16_UNORM image, row-major as the field is. */
		[[nodiscard]] assetlib::ImageData
		HeightImage(const assetlib::Heightfield& field)
		{
			const size_t rowPitch = static_cast<size_t>(field.samplesX) * sizeof(uint16_t);
			const size_t bytes    = rowPitch * field.samplesZ;

			auto image      = assetlib::ImageData();
			image.width     = field.samplesX;
			image.height    = field.samplesZ;
			image.mipLevels = 1;
			image.arraySize = 1;
			image.vkFormat  = assetlib::VkFormat::R16_UNORM;
			image.pixels    = core::fixed_buffer<std::byte>(bytes);
			std::memcpy(image.pixels.data(), field.heights.data(), bytes);
			image.subresources.push_back(
				assetlib::ImageSubresource{ .offset     = 0,
			                                .rowPitch   = rowPitch,
			                                .slicePitch = bytes });
			return image;
		}

		/**
		 * The lowest and highest world y of every node of every level, level-major: level 0 from
		 * the samples each patch spans, each level above from its children.
		 */
		[[nodiscard]] std::vector<idl::TerrainNodeBounds>
		NodeBounds(
			const assetlib::Heightfield& field,
			const float                  baseHeight,
			const uint32_t               levels)
		{
			const uint32_t sx = field.samplesX;
			const uint32_t sz = field.samplesZ;
			auto bounds = std::vector<idl::TerrainNodeBounds>(TerrainNodeCount(sx, sz, levels));

			const uint32_t acrossX = TerrainNodesAcross(sx, 0);
			const uint32_t acrossZ = TerrainNodesAcross(sz, 0);
			core::parallel_for(acrossZ, 0, "terrain node bounds", [&](const size_t nz) {
				for (uint32_t nx = 0; nx < acrossX; ++nx)
				{
					const uint32_t x0 = nx * idl::cTerrainPatchQuads;
					const uint32_t z0 = static_cast<uint32_t>(nz) * idl::cTerrainPatchQuads;
					const uint32_t x1 = std::min(x0 + idl::cTerrainPatchQuads, sx - 1);
					const uint32_t z1 = std::min(z0 + idl::cTerrainPatchQuads, sz - 1);

					uint16_t lo = 65535;
					uint16_t hi = 0;
					for (uint32_t z = z0; z <= z1; ++z)
					{
						const uint16_t* row = field.heights.data() + static_cast<size_t>(z) * sx;
						for (uint32_t x = x0; x <= x1; ++x)
						{
							lo = std::min(lo, row[x]);
							hi = std::max(hi, row[x]);
						}
					}
					const float scale         = field.heightRange / 65535.0f;
					bounds[nz * acrossX + nx] = idl::TerrainNodeBounds{
						.minY = baseHeight + static_cast<float>(lo) * scale,
						.maxY = baseHeight + static_cast<float>(hi) * scale,
					};
				}
			});

			uint32_t childFirst   = 0;
			uint32_t childAcrossX = acrossX;
			uint32_t childAcrossZ = acrossZ;
			for (uint32_t level = 1; level < levels; ++level)
			{
				const uint32_t first = childFirst + childAcrossX * childAcrossZ;
				const uint32_t ownX  = TerrainNodesAcross(sx, level);
				const uint32_t ownZ  = TerrainNodesAcross(sz, level);
				for (uint32_t nz = 0; nz < ownZ; ++nz)
				{
					for (uint32_t nx = 0; nx < ownX; ++nx)
					{
						auto bound = idl::TerrainNodeBounds{ .minY = 1e30f, .maxY = -1e30f };
						for (uint32_t cz = 2 * nz; cz < std::min(2 * nz + 2, childAcrossZ); ++cz)
						{
							for (uint32_t cx = 2 * nx; cx < std::min(2 * nx + 2, childAcrossX);
							     ++cx)
							{
								const idl::TerrainNodeBounds child =
									bounds[childFirst + cz * childAcrossX + cx];
								bound.minY = std::min(bound.minY, child.minY);
								bound.maxY = std::max(bound.maxY, child.maxY);
							}
						}
						bounds[first + nz * ownX + nx] = bound;
					}
				}
				childFirst   = first;
				childAcrossX = ownX;
				childAcrossZ = ownZ;
			}
			return bounds;
		}
	}

	TerrainHandle
	Scene::CreateTerrain(const TerrainDesc& desc)
	{
		ValidateTerrain(desc, m_Surfaces);
		const assetlib::Heightfield& field = *desc.heightfield;

		auto meta     = TerrainMeta();
		meta.material = desc.material;

		const uint32_t levels = TerrainLevels(field.samplesX, field.samplesZ);

		const float baseHeight = desc.origin.y + field.minHeight;

		meta.heights = m_Textures.Add(HeightImage(field), "Terrain Heights");
		if (meta.heights.textureSlot.is_null())
		{
			throw SceneError("CreateTerrain: the scene's texture pool is exhausted");
		}

		try
		{
			const std::vector<idl::TerrainNodeBounds> bounds =
				NodeBounds(field, baseHeight, levels);
			meta.nodeBounds =
				m_TerrainNodeBounds.Add(std::span<const idl::TerrainNodeBounds>(bounds));

			auto record = idl::Terrain();
			record.originAndCellSize =
				glm::vec4(desc.origin.x, baseHeight, desc.origin.z, field.cellSize);
			record.heightRangeAndPixelsPerCell =
				glm::vec4(field.heightRange, desc.pixelsPerCell, 0.0f, 0.0f);
			record.samplesX       = field.samplesX;
			record.samplesZ       = field.samplesZ;
			record.levels         = levels;
			record.materialOffset = desc.material.byteOffset;
			meta.record           = m_TerrainRecords.Add(record);

			auto slot = m_Terrains.try_allocate_and_emplace(meta);
			if (slot.is_null())
			{
				m_Terrains.grow(std::max(c_InitialTerrains, m_Terrains.capacity() * 2));
				slot = m_Terrains.allocate_and_emplace(meta);
			}

			++m_TerrainEpoch;
			++m_TemporalEpoch;
			return TerrainHandle{ slot };
		}
		catch (...)
		{
			if (!meta.record.is_null())
			{
				m_TerrainRecords.Erase(meta.record);
			}
			if (!meta.nodeBounds.is_null())
			{
				m_TerrainNodeBounds.Erase(meta.nodeBounds);
			}
			m_Textures.Delete(meta.heights);
			throw;
		}
	}

	void
	Scene::DeleteTerrain(const TerrainHandle terrain)
	{
		if (!IsTerrainAlive(terrain))
		{
			throw SceneError(
				"TerrainHandle passed to DeleteTerrain refers to a deleted or unknown terrain");
		}

		TerrainMeta& meta = m_Terrains[terrain.handle.index];
		ReleaseTerrainGrass(meta.grass);
		m_TerrainRecords.Erase(meta.record);
		m_TerrainNodeBounds.Erase(meta.nodeBounds);
		m_Textures.Delete(meta.heights);

		m_Terrains.release_slot(terrain.handle.index);
		++m_TerrainEpoch;
		++m_TemporalEpoch;
	}
}
