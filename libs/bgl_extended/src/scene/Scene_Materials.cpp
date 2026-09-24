#include "scene/Scene.h"
#include "uniforms/DescriptorHandle.h"
#include "util/util.h"
#include <RangeWithCount.h>
#include <RawEntry.h>
#include <algorithm>
#include <array>
#include <assetlib_structs/BMaterial.h>  // the channel layout the static_asserts below pin us to
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
#include <bgl/IScene.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/SurfaceType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/types/ChannelRouteDesc.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/GameSurfaceRecord.h>
#include <bgl_common/idl/LoosePbrMaterial.h>
#include <bgl_common/idl/PbrMaterial.h>
#include <bgl_common/idl/RawTextureHandle.h>
#include <core/containers/slot_handle.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>

#include <tuple>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		// A texture handle as the bytes a raw-loaded payload stores. The descriptor exposes no
		// accessor, so the conversion is a copy -- kept in one place, as DescriptorHandle keeps its
		// own inverse.
		idl::RawTextureHandle
		RawHandleOf(DescriptorHandle descriptor) noexcept
		{
			auto raw = idl::RawTextureHandle();
			std::memcpy(&raw, &descriptor, sizeof(raw));
			return raw;
		}
	}

	idl::RawTextureHandle
	Scene::ResolveTexture(TextureAssetHandle texture, core::slot_handle fallback) const
	{
		// The descriptor comes from the resource manager, not the slot: this one is read straight out
		// of GPU memory, so it has to be whatever the backend's shader can dereference -- as bytes,
		// since the record it lands in is raw-loaded and the arena's typed view is what samples them.
		const core::slot_handle slot = texture.textureSlot ? texture.textureSlot : fallback;

		return RawHandleOf(m_Textures.GetDescriptor(slot));
	}

	idl::PbrMaterial
	Scene::BuildPbrMaterial(const PbrMaterialDesc& desc) const
	{
		const auto white = m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite);
		const auto flatNormal =
			m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kFlatNormal);

		idl::PbrMaterial material{};
		material.baseColorTexture         = ResolveTexture(desc.baseColorTexture, white);
		material.normalTexture            = ResolveTexture(desc.normalTexture, flatNormal);
		material.ormTexture               = ResolveTexture(desc.ormTexture, white);
		material.geometryOcclusionTexture = ResolveTexture(desc.geometryOcclusionTexture, white);
		material.baseColorFactor          = desc.baseColorFactor;
		material.metallicFactor           = desc.metallicFactor;
		material.roughnessFactor          = desc.roughnessFactor;
		material.specular           = glm::vec4(desc.specularColorFactor, desc.specularFactor);
		material.transmissionFactor = desc.transmissionFactor;
		material.alphaCutoff        = desc.alphaCutoff;
		material.doubleSided        = desc.doubleSided ? 1u : 0u;

		return material;
	}

	Scene::BuiltSurfaceMaterial
	Scene::BuildSurfaceMaterial(const SurfaceMaterialDesc& desc) const
	{
		const auto found = std::ranges::find(m_Surfaces, desc.surface, &SurfaceType::name);
		if (found == m_Surfaces.end())
		{
			throw SceneError(
				std::format(
					"no surface named '{}' is registered; a surface is read from the client's "
					"shader "
					"directory when the graphics is created",
					desc.surface));
		}

		const SurfaceType&   surface = *found;
		const SurfaceParams& params  = surface.params;

		// ADR-5: a caller that says which contract it was authored against is refused a surface
		// that conforms to the other one, instead of silently drawing under a lighting model the
		// document never meant.
		if (desc.shading.has_value() && *desc.shading != surface.shading)
		{
			const auto name = [](SurfaceShading shading) {
				return shading == SurfaceShading::kLit ? "owns its lighting (ILitSurfaceSource)" :
				                                         "is lit by the engine (ISurfaceSource)";
			};
			throw SceneError(
				std::format(
					"surface '{}' {}, but the material expects one that {}",
					desc.surface,
					name(surface.shading),
					name(*desc.shading)));
		}

		const std::optional<uint32_t> carrier = CoverageCarrierSlot(params);

		// Hashed alpha relates a UV footprint to texels, so it needs one of the surface's textures
		// to measure against -- and a surface answers coverage with arithmetic the engine cannot
		// read a texture out of. Every other layer draws either way.
		if (desc.layerType == LayerType::kHashed && !carrier)
		{
			throw SceneError(
				std::format(
					"surface '{}' is asked for hashed alpha but declares no coverage carrier: "
					"hashed measures minification against a CoverageSlot, or a ColorSlot where "
					"alpha rides in the colour",
					desc.surface));
		}

		std::vector<std::byte> payload(sizeof(idl::GameSurfaceRecord) + params.byteSize);

		// The offsets are reflection's, so a field landing outside the block it was measured in is
		// an engine bug and not a caller's -- and one that would otherwise be a heap write rather
		// than a wrong pixel.
		const auto writeParam = [&payload](uint32_t byteOffset, const void* src, size_t bytes) {
			gassert(
				sizeof(idl::GameSurfaceRecord) + byteOffset + bytes <= payload.size(),
				"A surface field at {} spans {} bytes, past the block reflection measured",
				byteOffset,
				bytes);

			std::memcpy(payload.data() + sizeof(idl::GameSurfaceRecord) + byteOffset, src, bytes);
		};

		// Defaults first, so a value the material leaves alone still lands as the surface declared
		// it rather than as zero.
		for (const SurfaceValue& value : params.values)
		{
			const uint32_t components = SurfaceValueComponents(value.type);
			writeParam(value.byteOffset, &value.defaultValue.x, components * sizeof(float));
		}

		for (const SurfaceValueBinding& binding : desc.values)
		{
			const auto declared =
				std::ranges::find(params.values, binding.name, &SurfaceValue::name);
			if (declared == params.values.end())
			{
				// Declared, but as the other kind of field: a name that exists and cannot take a
				// number is worth saying out loud, since "no such value" would send its author
				// looking for a typo that is not there.
				const bool isTexture =
					std::ranges::find(params.textures, binding.name, &SurfaceTexture::name) !=
					params.textures.end();

				throw SceneError(
					isTexture ? std::format(
									"surface '{}' declares '{}' as a texture, not a value",
									desc.surface,
									binding.name) :
								std::format(
									"surface '{}' declares no value named '{}'",
									desc.surface,
									binding.name));
			}

			const uint32_t components = SurfaceValueComponents(declared->type);
			writeParam(declared->byteOffset, &binding.value.x, components * sizeof(float));
		}

		idl::GameSurfaceRecord record{};
		record.doubleSided = desc.doubleSided ? 1u : 0u;
		record.alphaCutoff = desc.alphaCutoff;

		// Written whatever the layer. A record carries no layer -- the handle does -- so this is the
		// surface's property, resolved once, and only the hashed rows read it.
		record.coverageSlot = carrier.value_or(idl::cNoCoverageSlot);

		// Every handle is filled, so a slot the material never named still samples something rather
		// than a null descriptor. What that something is comes from the kind the surface declared:
		// white is the identity for a colour or a factor, and the identity for a normal map is a
		// flat one -- the same two defaults the PBR path picks between.
		const auto white = m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite);
		const auto flatNormal =
			m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kFlatNormal);

		for (idl::RawTextureHandle& handle : record.textures)
			handle = RawHandleOf(m_Textures.GetDescriptor(white));

		// The identity routing over white: a data slot nothing binds gathers exactly what an
		// unbound whole slot samples, and a partially routed one fills its gaps with white.
		for (uint32_t route = 0;
		     route < idl::cGameSurfaceTextureSlots * idl::cGameSurfaceRouteChannels;
		     ++route)
		{
			record.routeTextures[route] = RawHandleOf(m_Textures.GetDescriptor(white));
			record.routeChannels[route] = route % idl::cGameSurfaceRouteChannels;
		}

		for (const SurfaceTexture& texture : params.textures)
		{
			if (texture.kind == SurfaceTextureKind::kNormal)
			{
				record.textures[texture.index] = RawHandleOf(m_Textures.GetDescriptor(flatNormal));
			}

			// The index the shader samples by is the engine's to write; the surface declared the
			// field and never sets it.
			const uint32_t index = texture.index;
			writeParam(texture.byteOffset, &index, sizeof(index));
		}

		for (const SurfaceTextureBinding& binding : desc.textures)
		{
			const auto declared =
				std::ranges::find(params.textures, binding.name, &SurfaceTexture::name);
			if (declared == params.textures.end())
			{
				const bool isValue =
					std::ranges::find(params.values, binding.name, &SurfaceValue::name) !=
					params.values.end();

				throw SceneError(
					isValue ? std::format(
								  "surface '{}' declares '{}' as a value, not a texture",
								  desc.surface,
								  binding.name) :
							  std::format(
								  "surface '{}' declares no texture named '{}'",
								  desc.surface,
								  binding.name));
			}

			const bool routed =
				std::ranges::any_of(binding.routes, [](const SurfaceChannelRoute& route) {
					return !route.texture.textureSlot.is_null();
				});

			if (routed && declared->kind != SurfaceTextureKind::kData)
			{
				throw SceneError(
					std::format(
						"surface '{}' declares '{}' as a slot that binds whole; routes compose "
						"data slots only",
						desc.surface,
						binding.name));
			}

			if (routed && binding.texture.textureSlot)
			{
				throw SceneError(
					std::format(
						"'{}' binds a texture and routes at once; a data slot is one or the other",
						binding.name));
			}

			if (routed)
			{
				record.routedMask |= 1u << declared->index;

				for (uint32_t c = 0; c < idl::cGameSurfaceRouteChannels; ++c)
				{
					const SurfaceChannelRoute& route = binding.routes[c];
					if (route.texture.textureSlot.is_null())
						continue;

					if (route.channel >= idl::cGameSurfaceRouteChannels)
					{
						throw SceneError(
							std::format(
								"'{}' routes component {} from channel {}; a texture has "
								"channels 0..3",
								binding.name,
								c,
								route.channel));
					}

					const uint32_t at = declared->index * idl::cGameSurfaceRouteChannels + c;
					record.routeTextures[at] =
						RawHandleOf(m_Textures.GetDescriptor(route.texture.textureSlot));
					record.routeChannels[at] = route.channel;
				}
				continue;
			}

			if (binding.texture.textureSlot)
			{
				record.textures[declared->index] =
					RawHandleOf(m_Textures.GetDescriptor(binding.texture.textureSlot));

				// The reader only ever gathers a data slot, so a whole binding on one becomes
				// its identity routing here.
				if (declared->kind == SurfaceTextureKind::kData)
				{
					for (uint32_t c = 0; c < idl::cGameSurfaceRouteChannels; ++c)
					{
						const uint32_t at = declared->index * idl::cGameSurfaceRouteChannels + c;
						record.routeTextures[at] = record.textures[declared->index];
						record.routeChannels[at] = c;
					}
				}
			}
		}

		std::memcpy(payload.data(), &record, sizeof(record));

		return { surface.kind, std::move(payload) };
	}

	MaterialHandle
	Scene::CreateSurfaceMaterial(const SurfaceMaterialDesc& desc)
	{
		const BuiltSurfaceMaterial built = BuildSurfaceMaterial(desc);
		const idl::RawEntry        entry = m_Materials.AddRecord(built.kind, built.payload);

		return MaterialHandle{ built.kind, desc.layerType, entry.byteOffset };
	}

	void
	Scene::UpdateSurfaceMaterial(MaterialHandle material, const SurfaceMaterialDesc& desc)
	{
		// The handle first, as UpdatePbrMaterial checks it: a caller holding a stale one should hear
		// about that rather than about whatever its desc says.
		if (!m_Materials.IsOffsetValid(material.byteOffset))
		{
			throw SceneError(
				"MaterialHandle passed to UpdateSurfaceMaterial is invalid or expired");
		}

		if (m_Materials.GetTagAt(material.byteOffset) != material.materialType)
		{
			throw SceneError(
				"MaterialHandle passed to UpdateSurfaceMaterial names a record of another kind");
		}

		const BuiltSurfaceMaterial built = BuildSurfaceMaterial(desc);

		// The surface cannot change: it is what the record's kind and its size were fixed by, and a
		// submesh keeps the byte offset either way.
		if (material.materialType != built.kind)
		{
			throw SceneError(
				std::format(
					"MaterialHandle passed to UpdateSurfaceMaterial was not created with surface "
					"'{}'",
					desc.surface));
		}

		m_Materials.SetRecordPayload(idl::RawEntry{ material.byteOffset }, built.payload);

		// Every rewrite counts, including one landing on the bytes already there: an entry is a
		// GPU-layout mirror whose padding no comparison can trust.
		++m_TemporalEpoch;
	}

	MaterialHandle
	Scene::CreatePbrMaterial(const PbrMaterialDesc& desc)
	{
		const idl::PbrMaterial material = BuildPbrMaterial(desc);
		const idl::RawEntry    entry =
			m_Materials.AddRecord(MaterialType::kPBR, std::as_bytes(std::span(&material, 1)));

		return MaterialHandle{ MaterialType::kPBR, desc.layerType, entry.byteOffset };
	}

	void
	Scene::UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc)
	{
		if (material.materialType != MaterialType::kPBR)
		{
			throw SceneError("MaterialHandle passed to UpdatePbrMaterial is not a kPBR material");
		}
		if (!m_Materials.IsOffsetValid(material.byteOffset))
		{
			throw SceneError(
				"MaterialHandle passed to UpdatePbrMaterial has expired or is invalid");
		}

		// The record says what it is, and the size guard alone would not: a stale handle whose
		// bytes were recycled into a larger record of another kind fits inside it.
		if (m_Materials.GetTagAt(material.byteOffset) != MaterialType::kPBR)
		{
			throw SceneError(
				"MaterialHandle passed to UpdatePbrMaterial names a record of another type");
		}

		// Rewriting the payload is all it takes: a submesh stores the material's byte offset, so
		// every submesh bound to this material picks the new contents up with no rebinding. The
		// record keeps its offset and its tag, so caller-held handles stay valid, and the
		// bucket -- which derives from materialType, not from the desc -- cannot change.
		const idl::PbrMaterial rebuilt = BuildPbrMaterial(desc);
		m_Materials.SetRecordPayload(
			idl::RawEntry{ material.byteOffset },
			std::as_bytes(std::span(&rebuilt, 1)));

		// Every rewrite counts, including one landing on the bytes already there: an entry is a
		// GPU-layout mirror whose padding no comparison can trust.
		++m_TemporalEpoch;
	}

	idl::LoosePbrMaterial
	Scene::BuildLoosePbrMaterial(const LoosePbrMaterialDesc& desc) const
	{
		const auto white = m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite);
		const auto flatNormal =
			m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kFlatNormal);

		// A routed channel resolves to (its texture's bindless index, its channel). An unrouted
		// channel falls back to a default texture + channel chosen so the sampled value matches the
		// PbrMaterial default for that output: white (1.0) for base color / ORM, and the flat-normal
		// texture (R,G = 0.5) for normal X / Y.
		const auto resolve = [this](
								 idl::LoosePbrMaterial&  material,
								 idl::PbrChannel         channel,
								 const ChannelRouteDesc& route,
								 core::slot_handle       fallbackTex,
								 uint16_t                fallbackChannel) {
			const bool              routed = !route.texture.textureSlot.is_null();
			const core::slot_handle slot   = routed ? route.texture.textureSlot : fallbackTex;
			const size_t            i      = static_cast<size_t>(channel);

			material.textures[i] = RawHandleOf(m_Textures.GetDescriptor(slot));
			material.channels[i] = routed ? route.channel : fallbackChannel;
		};

		// The shader finds a payload's textures by position -- handle i at payloadOffset + i * 8 --
		// so a reordered field is a silently swapped map rather than a compile error. These are what
		// make it one. See MaterialData.slang's RecordTexture.
		static_assert(offsetof(idl::PbrMaterial, baseColorTexture) == 0);
		static_assert(offsetof(idl::PbrMaterial, normalTexture) == sizeof(idl::RawTextureHandle));
		static_assert(offsetof(idl::PbrMaterial, ormTexture) == 2 * sizeof(idl::RawTextureHandle));
		static_assert(
			offsetof(idl::PbrMaterial, geometryOcclusionTexture) ==
			3 * sizeof(idl::RawTextureHandle));
		static_assert(offsetof(idl::LoosePbrMaterial, textures) == 0);
		static_assert(
			sizeof(idl::LoosePbrMaterial::textures) ==
			idl::cLooseChannelCount * sizeof(idl::RawTextureHandle));
		static_assert(
			offsetof(idl::LoosePbrMaterial, geometryOcclusionTexture) ==
			idl::cLooseChannelCount * sizeof(idl::RawTextureHandle));
		static_assert(offsetof(idl::GameSurfaceRecord, textures) == 0);
		static_assert(
			sizeof(idl::GameSurfaceRecord::textures) ==
			idl::cGameSurfaceTextureSlots * sizeof(idl::RawTextureHandle));
		// The routes ride the same typed view as the slot handles, contiguously, indexed
		// cGameSurfaceTextureSlots past them; their channel selectors sit where the shader's
		// constant says. A gap in either is a gather off a neighbouring record's bytes.
		static_assert(
			offsetof(idl::GameSurfaceRecord, routeTextures) ==
			sizeof(idl::GameSurfaceRecord::textures));
		static_assert(
			idl::cRawPayloadOffset + offsetof(idl::GameSurfaceRecord, routeChannels) ==
			idl::cGameSurfaceRouteChannelsByteOffset);
		static_assert(
			idl::cRawPayloadOffset + offsetof(idl::GameSurfaceRecord, routedMask) ==
			idl::cGameSurfaceRoutedMaskByteOffset);
		// The contract's route array is four wide because a sample is; the record agrees.
		static_assert(
			std::tuple_size_v<decltype(SurfaceTextureBinding::routes)> ==
			idl::cGameSurfaceRouteChannels);
		// A game surface's parameters follow the fixed part at an offset the shader holds as a
		// constant; the struct growing without it is a record read one field late.
		static_assert(
			idl::cRawPayloadOffset + sizeof(idl::GameSurfaceRecord) ==
			idl::cGameSurfaceParamsByteOffset);

		// The other half of that arithmetic: the payload stores RawTextureHandle while the view is
		// strided by the handle itself, and a payload offset that is not a whole number of handles
		// truncates the division into the middle of a neighbouring one.
		static_assert(sizeof(DescriptorHandle) == sizeof(idl::RawTextureHandle));
		static_assert(idl::cRawPayloadOffset % sizeof(idl::RawTextureHandle) == 0);

		// The GPU's channel order is generated from the IDL; the file's is declared in BMaterial.h. They
		// describe the same nine routes, so a mismatch would silently sample the wrong map -- roughness
		// read as metallic, say. Pin them together rather than trusting two lists to stay in step.
		static_assert(
			idl::cLooseChannelCount == assetlib::c_LooseChannelCount,
			"The GPU and the .bmaterial file must agree on how many loose channels there are");
		// assetlib::channelIndex is the same cast and lives in assetlib, which this does not link:
		// assetlib_structs is data, so a question about a container is answered a library up.
		static_assert(
			static_cast<size_t>(idl::PbrChannel::kBaseColorR) ==
					static_cast<size_t>(assetlib::PbrChannel::kBaseColorR) &&
				static_cast<size_t>(idl::PbrChannel::kAo) ==
					static_cast<size_t>(assetlib::PbrChannel::kAo) &&
				static_cast<size_t>(idl::PbrChannel::kNormalX) ==
					static_cast<size_t>(assetlib::PbrChannel::kNormalX),
			"idl::PbrChannel and assetlib::PbrChannel must index BMaterial::routes identically");

		auto material = idl::LoosePbrMaterial();
		// Base color R,G,B,A -> white (any channel samples 1.0).
		resolve(material, idl::PbrChannel::kBaseColorR, desc.baseColor[0], white, 0);
		resolve(material, idl::PbrChannel::kBaseColorG, desc.baseColor[1], white, 0);
		resolve(material, idl::PbrChannel::kBaseColorB, desc.baseColor[2], white, 0);
		resolve(material, idl::PbrChannel::kBaseColorA, desc.baseColor[3], white, 0);
		// ORM ao,roughness,metallic -> white (1.0; factors drive rough/metal).
		resolve(material, idl::PbrChannel::kAo, desc.orm[0], white, 0);
		resolve(material, idl::PbrChannel::kRoughness, desc.orm[1], white, 0);
		resolve(material, idl::PbrChannel::kMetallic, desc.orm[2], white, 0);
		// Normal X,Y -> flat-normal texture (R = 0.5, G = 0.5) -> decoded (0,0,1).
		resolve(material, idl::PbrChannel::kNormalX, desc.normal[0], flatNormal, 0);
		resolve(material, idl::PbrChannel::kNormalY, desc.normal[1], flatNormal, 1);

		material.geometryOcclusionTexture = ResolveTexture(desc.geometryOcclusionTexture, white);

		material.baseColorFactor    = desc.baseColorFactor;
		material.metallicFactor     = desc.metallicFactor;
		material.roughnessFactor    = desc.roughnessFactor;
		material.specular           = glm::vec4(desc.specularColorFactor, desc.specularFactor);
		material.transmissionFactor = desc.transmissionFactor;
		material.alphaCutoff        = desc.alphaCutoff;
		material.doubleSided        = desc.doubleSided ? 1u : 0u;

		return material;
	}

	MaterialHandle
	Scene::CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc)
	{
		const idl::LoosePbrMaterial material = BuildLoosePbrMaterial(desc);
		const idl::RawEntry         entry =
			m_Materials.AddRecord(MaterialType::kLoosePbr, std::as_bytes(std::span(&material, 1)));

		return MaterialHandle{ MaterialType::kLoosePbr, desc.layerType, entry.byteOffset };
	}

	void
	Scene::UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc)
	{
		if (material.materialType != MaterialType::kLoosePbr)
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial is not a kLoosePbr material");
		}
		if (!m_Materials.IsOffsetValid(material.byteOffset))
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial has expired or is invalid");
		}

		// The record says what it is, and the size guard alone would not: a stale handle whose
		// bytes were recycled into a larger record of another kind fits inside it.
		if (m_Materials.GetTagAt(material.byteOffset) != MaterialType::kLoosePbr)
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial names a record of another type");
		}

		// See UpdatePbrMaterial: the payload is rewritten in place, so every submesh bound to this
		// material follows it and the handle stays valid, and every rewrite moves the shading epoch.
		const idl::LoosePbrMaterial rebuilt = BuildLoosePbrMaterial(desc);
		m_Materials.SetRecordPayload(
			idl::RawEntry{ material.byteOffset },
			std::as_bytes(std::span(&rebuilt, 1)));

		++m_TemporalEpoch;
	}

	void
	Scene::DeleteMaterial(MaterialHandle material)
	{
		// A kind with an arena record can be freed. kNull and kAssert name shading behaviour, not
		// an entry in a buffer, so there is nothing to release.
		// A surface's kind is kGameStart + its slot, so the switch below sees only the first.
		const bool gameKind = GameSlot(material.materialType).has_value();

		switch (gameKind ? MaterialType::kGameStart : material.materialType)
		{
		case MaterialType::kPBR:
		case MaterialType::kLoosePbr:
		case MaterialType::kGameStart:
			if (!m_Materials.IsOffsetValid(material.byteOffset))
			{
				throw SceneError(
					"MaterialHandle passed to DeleteMaterial has expired or is invalid");
			}

			// The record says what it is, so every kind frees the same way -- and the tag is what
			// catches a handle whose type says one thing and whose offset holds another.
			if (m_Materials.GetTagAt(material.byteOffset) != material.materialType)
			{
				throw SceneError(
					"MaterialHandle passed to DeleteMaterial names a record of another type");
			}

			m_Materials.Erase(material.byteOffset);
			++m_TemporalEpoch;
			return;

		case MaterialType::kInvalid:
		case MaterialType::kNull:
		case MaterialType::kAssert:
			break;
		}

		throw SceneError("MaterialHandle passed to DeleteMaterial has no material storage");
	}

	void
	Scene::SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material)
	{
		if (geom.geomType == GeomType::kInvalid || geom.geomType == GeomType::kCount)
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial has no valid geom type");
		}
		if (!IsGeomAlive(geom))
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial has expired or is invalid");
		}
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to SetSubmeshMaterial");
		}
		if (!AcceptsMaterial(geom.geomType, material))
		{
			throw SceneError(
				"SetSubmeshMaterial: animated geometry takes a baked PBR or a game surface "
				"material -- neither animated pipeline has an unlit or loose variant");
		}

		const idl::RangeWithCount& submeshes = m_Geoms[geom.handle.index].submeshes;
		if (submeshIndex >= submeshes.count)
		{
			throw SceneError("submeshIndex passed to SetSubmeshMaterial is out of range");
		}

		// Nothing is uploaded: the epoch is what carries this to instances already placed.
		m_SubmeshBuffer.MetaAt(submeshes.range.offsetStart)[submeshIndex] = material;
		++m_MaterialEpoch;
		++m_TemporalEpoch;
	}
}
