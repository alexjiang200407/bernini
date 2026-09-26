#include "scene/GeomRollback.h"
#include "scene/Scene.h"
#include <algorithm>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
#include <bgl/GrassHandle.h>
#include <bgl/IScene.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <bgl/glm.h>
#include <bgl/types/GrassDesc.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/GrassChunk.h>
#include <bgl_common/idl/GrassClump.h>
#include <bgl_common/idl/GrassLook.h>
#include <cmath>
#include <core/containers/multi_slot_handle.h>
#include <core/math.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_InitialGrassLooks = 4;

		// One number declared twice, because bgl does not link assetlib, as cMeshletsPerGroup is.
		static_assert(assetlib::c_GrassClumpsPerChunk == idl::cGrassClumpsPerChunk);

		/**
		 * `n` as two snorm16 on the octahedron, x in the low half. The grass stage's DecodeOctahedral
		 * is the inverse; the two must fold on the same axis.
		 */
		[[nodiscard]] uint32_t
		EncodeOctahedral(const glm::vec3& n) noexcept
		{
			const glm::vec3 unit = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));

			glm::vec2 folded(unit.x, unit.y);
			if (unit.z < 0.0f)
			{
				const glm::vec2 sign(unit.x >= 0.0f ? 1.0f : -1.0f, unit.y >= 0.0f ? 1.0f : -1.0f);
				folded = (1.0f - glm::abs(glm::vec2(unit.y, unit.x))) * sign;
			}

			const auto snorm = [](const float v) {
				return static_cast<uint32_t>(static_cast<uint16_t>(
					static_cast<int16_t>(std::lround(glm::clamp(v, -1.0f, 1.0f) * 32767.0f))));
			};
			return snorm(folded.x) | (snorm(folded.y) << 16u);
		}

		[[nodiscard]] uint32_t
		PackColor(const glm::u8vec4& color) noexcept
		{
			return static_cast<uint32_t>(color.r) | (static_cast<uint32_t>(color.g) << 8u) |
			       (static_cast<uint32_t>(color.b) << 16u) |
			       (static_cast<uint32_t>(color.a) << 24u);
		}

		[[nodiscard]] bool
		IsShare(const float value) noexcept
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		}

		[[nodiscard]] bool
		IsPositive(const float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		[[nodiscard]] bool
		IsNonNegative(const float value) noexcept
		{
			return std::isfinite(value) && value >= 0.0f;
		}

		[[nodiscard]] bool
		IsColor(const glm::vec3& color) noexcept
		{
			return core::is_finite(color) && color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f;
		}

		/**
		 * Refuses a field on mesh `meshIndex` whose ranges the file cannot back, before anything
		 * reads them.
		 */
		void
		CheckGrassRanges(const assetlib::BGrassFields& fields, const uint32_t meshIndex)
		{
			for (size_t f = 0; f < fields.fields.size(); ++f)
			{
				const assetlib::GrassField& field = fields.fields[f];
				if (field.mesh != meshIndex)
				{
					continue;
				}

				if (field.chunkCount == 0)
				{
					throw SceneError(std::format("AttachGrass: field {} has no chunks", f));
				}

				// One amplification group per chunk.
				if (field.chunkCount > idl::cMaxDispatchMeshGroups)
				{
					throw SceneError(
						std::format(
							"AttachGrass: field {} has {} chunks, more than the {} thread groups "
							"one dispatch can launch",
							f,
							field.chunkCount,
							idl::cMaxDispatchMeshGroups));
				}

				if (static_cast<uint64_t>(field.firstChunk) + field.chunkCount >
				    fields.chunks.size())
				{
					throw SceneError(
						std::format(
							"AttachGrass: field {} claims {} chunks at offset {}, past the end of "
							"the {} there are",
							f,
							field.chunkCount,
							field.firstChunk,
							fields.chunks.size()));
				}

				for (uint32_t c = 0; c < field.chunkCount; ++c)
				{
					const assetlib::GrassChunk& chunk = fields.chunks[field.firstChunk + c];
					if (chunk.clumpCount == 0 ||
					    chunk.clumpCount > assetlib::c_GrassClumpsPerChunk ||
					    static_cast<uint64_t>(chunk.firstClump) + chunk.clumpCount >
					        fields.clumps.size())
					{
						throw SceneError(
							std::format(
								"AttachGrass: field {} chunk {} holds no clumps, more than {}, or "
								"clumps past the end of the {} there are",
								f,
								c,
								assetlib::c_GrassClumpsPerChunk,
								fields.clumps.size()));
					}
				}
			}
		}
	}

	void
	Scene::ValidateGrass(const GrassDesc& desc, const std::string_view caller)
	{
		const auto refuse = [caller](const std::string_view why) {
			throw SceneError(std::format("{}: {}", caller, why));
		};

		const MaterialType kind = desc.material.materialType;
		if (!desc.material.IsValid() || kind == MaterialType::kNull ||
		    kind == MaterialType::kAssert)
		{
			refuse("the material must be a drawable one (not null, kNull or kAssert)");
		}
		if (desc.material.layerType == LayerType::kBlend)
		{
			refuse("a blended material has no opaque program for a solid blade to draw through");
		}

		const GrassBladeDesc& blade = desc.blade;
		if (!IsPositive(blade.minHeight) || !IsPositive(blade.maxHeight) ||
		    blade.minHeight > blade.maxHeight)
		{
			refuse("blade heights must be finite, positive and minHeight <= maxHeight");
		}
		if (!IsPositive(blade.rootWidth))
		{
			refuse("blade.rootWidth must be finite and positive");
		}
		if (!IsShare(blade.tipWidth) || !IsShare(blade.curvature) || !IsShare(blade.lean))
		{
			refuse("blade.tipWidth, curvature and lean must each be in [0, 1]");
		}
		if (blade.farSegments < 1 || blade.farSegments > blade.nearSegments ||
		    blade.nearSegments > c_MaxGrassBladeSegments)
		{
			refuse(
				std::format(
					"blade segments must be 1 <= farSegments <= nearSegments <= {}",
					c_MaxGrassBladeSegments));
		}

		if (desc.clump.bladesPerClump < 1 || desc.clump.bladesPerClump > c_MaxGrassBladesPerClump)
		{
			refuse(
				std::format("clump.bladesPerClump must be in [1, {}]", c_MaxGrassBladesPerClump));
		}
		if (!IsNonNegative(desc.clump.radius))
		{
			refuse("clump.radius must be finite and non-negative");
		}

		const GrassDensityDesc& density = desc.density;
		if (!IsNonNegative(density.fadeStart) || !std::isfinite(density.fadeEnd) ||
		    density.fadeEnd <= density.fadeStart)
		{
			refuse("density fades must be finite, non-negative and fadeStart < fadeEnd");
		}
		if (!IsNonNegative(density.widening))
		{
			refuse("density.widening must be finite and non-negative");
		}

		if (!IsShare(desc.response.stiffness) || !IsNonNegative(desc.response.gustResponse))
		{
			refuse(
				"response.stiffness must be in [0, 1] and response.gustResponse finite and "
				"non-negative");
		}

		const GrassLightingDesc& lighting = desc.lighting;
		if (!IsShare(lighting.rootOcclusion) || !IsShare(lighting.normalRounding) ||
		    !IsShare(lighting.groundNormalNear) || !IsShare(lighting.groundNormalFar))
		{
			refuse(
				"lighting.rootOcclusion, normalRounding and both ground-normal blends must be "
				"in [0, 1]");
		}
		if (!IsColor(lighting.translucencyColor) || !IsNonNegative(lighting.translucency))
		{
			refuse("lighting translucency and its colour must be finite and non-negative");
		}

		if (!IsColor(desc.color.rootTint) || !IsColor(desc.color.tipTint) ||
		    !IsShare(desc.color.variation))
		{
			refuse("colour tints must be finite and non-negative, and variation in [0, 1]");
		}
	}

	GrassHandle
	Scene::CreateGrass(const GrassDesc& desc)
	{
		ValidateGrass(desc, "CreateGrass");

		const auto meta =
			GrassMeta{ .desc = desc, .entry = m_GrassLooks.Add(BuildGrassLook(desc)) };
		try
		{
			auto slot = m_Grass.try_allocate_and_emplace(meta);
			if (slot.is_null())
			{
				m_Grass.grow(std::max(c_InitialGrassLooks, m_Grass.capacity() * 2));
				slot = m_Grass.allocate_and_emplace(meta);
			}
			return GrassHandle{ slot };
		}
		catch (...)
		{
			m_GrassLooks.Erase(meta.entry);
			throw;
		}
	}

	void
	Scene::UpdateGrass(const GrassHandle grass, const GrassDesc& desc)
	{
		if (!IsGrassAlive(grass))
		{
			throw SceneError(
				"GrassHandle passed to UpdateGrass refers to a deleted or unknown look");
		}

		ValidateGrass(desc, "UpdateGrass");

		GrassMeta& meta = m_Grass[grass.handle.index];
		meta.desc       = desc;
		m_GrassLooks.Set(meta.entry, BuildGrassLook(desc));

		// The material may have changed type, which is what a view groups its draws by.
		++m_GrassEpoch;
		++m_TemporalEpoch;
	}

	void
	Scene::DeleteGrass(const GrassHandle grass)
	{
		if (!IsGrassAlive(grass))
		{
			throw SceneError(
				"GrassHandle passed to DeleteGrass refers to a deleted or unknown look");
		}

		if (m_Grass[grass.handle.index].useCount > 0)
		{
			throw SceneError(
				"GrassHandle passed to DeleteGrass is still bound by a live geom; delete it first");
		}

		m_GrassLooks.Erase(m_Grass[grass.handle.index].entry);
		m_Grass.release_slot(grass.handle.index);
	}

	void
	Scene::AttachGrass(
		const GeomHandle                   geom,
		const assetlib::BGrassFields&      fields,
		const uint32_t                     meshIndex,
		const std::span<const GrassHandle> looks)
	{
		if (geom.geomType != GeomType::kStaticMesh || !IsGeomAlive(geom))
		{
			throw SceneError("AttachGrass: the geom is dead or not a static geom");
		}

		CheckGrassRanges(fields, meshIndex);

		struct BoundField
		{
			const assetlib::GrassField* field;
			GrassHandle                 look;
		};
		auto bound = std::vector<BoundField>();
		for (const assetlib::GrassField& field : fields.fields)
		{
			const GrassHandle look = field.mesh == meshIndex && field.look < looks.size() ?
			                             looks[field.look] :
			                             GrassHandle{};
			if (!look.IsValid())
			{
				continue;
			}

			if (!IsGrassAlive(look))
			{
				throw SceneError(
					std::format(
						"AttachGrass: the look bound to slot {} has been deleted",
						field.look));
			}

			bound.push_back(BoundField{ .field = &field, .look = look });
		}

		// Nothing below is the scene's until Commit(); see GeomRollback.
		auto rollback = GeomRollback();
		auto records  = std::vector<GrassFieldRecord>();
		for (const auto& [fieldPtr, look] : bound)
		{
			const assetlib::GrassField& field = *fieldPtr;

			auto clumps = std::vector<idl::GrassClump>();
			auto chunks = std::vector<idl::GrassChunk>();
			chunks.reserve(field.chunkCount);
			for (uint32_t c = 0; c < field.chunkCount; ++c)
			{
				const assetlib::GrassChunk& src = fields.chunks[field.firstChunk + c];

				auto& chunk          = chunks.emplace_back();
				chunk.boundingSphere = glm::vec4(src.boundingCenter, src.boundingRadius);
				chunk.firstClump     = static_cast<uint32_t>(clumps.size());
				chunk.clumpCount     = src.clumpCount;
				chunk.maxHeightScale = src.maxHeightScale;

				for (uint32_t k = 0; k < src.clumpCount; ++k)
				{
					const assetlib::GrassClump& point = fields.clumps[src.firstClump + k];

					auto& clump    = clumps.emplace_back();
					clump.position = glm::vec4(point.position, point.heightScale);
					clump.normal   = EncodeOctahedral(point.normal);
					clump.color    = PackColor(point.color);
				}
			}

			const core::multi_slot_handle clumpRange = rollback.Track(
				m_GrassClumps,
				m_GrassClumps.Add(std::span<const idl::GrassClump>(clumps)));
			for (idl::GrassChunk& chunk : chunks)
			{
				chunk.firstClump += clumpRange.index;
			}

			records.push_back(
				GrassFieldRecord{
					.look   = look,
					.chunks = rollback.Track(
						m_GrassChunks,
						m_GrassChunks.Add(std::span<const idl::GrassChunk>(chunks))),
					.clumps     = clumpRange,
					.chunkCount = field.chunkCount,
				});
		}

		GeomRecord& record = m_Geoms[geom.handle.index];
		ReleaseGrass(record.grass);
		for (const GrassFieldRecord& field : records)
		{
			++m_Grass[field.look.handle.index].useCount;
		}
		record.grass = std::move(records);

		rollback.Commit();
		++m_GrassEpoch;
	}

	void
	Scene::ReleaseGrass(std::vector<GrassFieldRecord>& fields) noexcept
	{
		for (const GrassFieldRecord& field : fields)
		{
			gassert(
				IsGrassAlive(field.look),
				"a live geom binds a grass look that is already gone");
			if (IsGrassAlive(field.look) && m_Grass[field.look.handle.index].useCount > 0)
			{
				--m_Grass[field.look.handle.index].useCount;
			}

			m_GrassChunks.Erase(field.chunks);
			m_GrassClumps.Erase(field.clumps);
		}

		if (!fields.empty())
		{
			fields.clear();
			++m_GrassEpoch;
		}
	}

	idl::GrassLook
	Scene::BuildGrassLook(const GrassDesc& desc) noexcept
	{
		auto look = idl::GrassLook();

		look.translucency = glm::vec4(desc.lighting.translucencyColor, desc.lighting.translucency);
		look.rootTint     = glm::vec4(desc.color.rootTint, 0.0f);
		look.tipTint      = glm::vec4(desc.color.tipTint, desc.color.variation);

		look.minHeight = desc.blade.minHeight;
		look.maxHeight = desc.blade.maxHeight;
		look.rootWidth = desc.blade.rootWidth;
		look.tipWidth  = desc.blade.tipWidth;
		look.curvature = desc.blade.curvature;
		look.lean      = desc.blade.lean;

		look.clumpRadius = desc.clump.radius;
		look.widening    = desc.density.widening;
		look.fadeStart   = desc.density.fadeStart;
		look.fadeEnd     = desc.density.fadeEnd;

		look.stiffness    = desc.response.stiffness;
		look.gustResponse = desc.response.gustResponse;

		look.rootOcclusion    = desc.lighting.rootOcclusion;
		look.normalRounding   = desc.lighting.normalRounding;
		look.groundNormalNear = desc.lighting.groundNormalNear;
		look.groundNormalFar  = desc.lighting.groundNormalFar;

		look.nearSegments   = desc.blade.nearSegments;
		look.farSegments    = desc.blade.farSegments;
		look.bladesPerClump = desc.clump.bladesPerClump;
		look.materialOffset = desc.material.byteOffset;
		return look;
	}
}
