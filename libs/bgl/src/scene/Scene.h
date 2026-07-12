#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "types/SubmeshInstance.h"
#include <bgl/IScene.h>
#include <core/containers/slot_vector.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;

	class Scene : public core::RefCounter<IScene>
	{
	public:
		enum class StandardSampler : uint32_t
		{
			kAnisoLinearWrap,
			kLinearClamp,
			kCount
		};

		Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager);
		~Scene() noexcept override { logger::trace("~Scene"); }
		Scene(const Scene&) noexcept = delete;
		Scene(Scene&&) noexcept      = delete;

		Scene&
		operator=(const Scene&) noexcept = delete;

		Scene&
		operator=(Scene&&) noexcept = delete;

		const SceneDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		auto
		GetBuffers()
		{
			return std::tie(
				m_SubmeshBuffer,
				m_MeshletBuffer,
				m_VertexMapBuffer,
				m_VertexDataBuffer,
				m_IndexBuffer,
				m_Pbr,
				m_Loose);
		}

		// --- SceneView support -------------------------------------------------
		// Instances live in SceneViews and reference this Scene's geometry by value: a view copies
		// the submesh range below into its per-placement Mesh. The Scene keeps no record of who
		// placed what, so the caller owns the ordering -- see IScene::DeleteGeom.

		[[nodiscard]] bool
		IsGeomAlive(GeomHandle geom) const noexcept override
		{
			return geom.IsValid() && m_GeomSubmeshes.valid(geom.handle);
		}

		// The submesh range a SceneView copies into a per-placement Mesh at instance-creation time.
		// Only valid while the geom is alive; check IsGeomAlive first.
		[[nodiscard]] const idl::RangeWithCount&
		GetGeomSubmeshes(uint32_t index) const noexcept
		{
			return m_GeomSubmeshes[index];
		}

		[[nodiscard]] const std::string&
		ResourceNamespace() const noexcept
		{
			return m_NamePrefix;
		}

		[[nodiscard]] SamplerHandle
		GetSampler(StandardSampler kind) const noexcept
		{
			return m_Samplers[static_cast<size_t>(kind)];
		}

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames);

		void
		Update(ICommandList* cmdList);

		GeomHandle
		AddCubeGeom(MaterialHandle material = {}) override;

		GeomHandle
		AddSphereGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          radius,
			MaterialHandle material = {}) override;

		GeomHandle
		AddStaticMesh(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials) override;

		TextureAssetHandle
		AddTextureAsset(assetlib::ImageData img, std::string debugName = "") override;

		void
		DeleteTextureAsset(TextureAssetHandle texture) override;

		MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) override;

		MaterialHandle
		CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc) override;

		void
		UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc) override;

		void
		UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc) override;

		void
		DeleteMaterial(MaterialHandle material) override;

		void
		SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material)
			override;

		void
		DeleteGeom(GeomHandle geom) override;

	private:
		// The desc -> GPU-struct conversion, shared by Create* and Update*, so a material built by
		// either route is byte-identical (including the default-texture fallbacks for absent maps).
		[[nodiscard]] idl::PbrMaterial
		BuildPbrMaterial(const PbrMaterialDesc& desc) const;

		[[nodiscard]] idl::LoosePbrMaterial
		BuildLoosePbrMaterial(const LoosePbrMaterialDesc& desc) const;

		SceneDesc   m_Desc;
		std::string m_NamePrefix;

		// One entry per live geom: where its submeshes sit in m_SubmeshBuffer. The slot generation is
		// what makes a GeomHandle expire when its geom is deleted (see IsGeomAlive).
		core::slot_vector<idl::RangeWithCount> m_GeomSubmeshes;

		RangeBuffer<idl::Submesh> m_SubmeshBuffer;
		RangeBuffer<idl::Meshlet> m_MeshletBuffer;
		RangeBuffer<uint32_t>     m_VertexMapBuffer;
		RangeBuffer<uint32_t>     m_VertexDataBuffer;
		RangeBuffer<uint32_t>     m_IndexBuffer;

		EntryBuffer<idl::PbrMaterial>      m_Pbr;
		EntryBuffer<idl::LoosePbrMaterial> m_Loose;

		std::array<SamplerHandle, static_cast<size_t>(StandardSampler::kCount)> m_Samplers;

		// 1x1 defaults a PbrMaterial falls back to per channel: white base/ORM, flat
		// tangent-space normal (0.5,0.5,1).
		enum class DefaultTexture : uint32_t
		{
			kWhite,
			kFlatNormal,
			kCount
		};
		std::array<TextureHandle, static_cast<size_t>(DefaultTexture::kCount)> m_DefaultTextures;

		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
