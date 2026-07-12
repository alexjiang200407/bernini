#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/PsoType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/util.h>
#include <core/containers/slot_handle.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class SceneError : public ApiError
	{
	public:
		SceneError() = delete;
		using ApiError::ApiError;
	};

	struct SceneDesc
	{
		uint32_t maxGeom                 = 1;
		uint32_t maxMeshlets             = 1;
		uint32_t maxIndices              = 1;
		uint32_t maxSubmeshes            = 1;
		uint32_t maxVertexBufferByteSize = 1;
		uint32_t maxPbrMaterials         = 1;
		uint32_t maxLoosePbrMaterials    = 1;
	};

	// Decoded IBL images (two cube maps + the 2D BRDF LUT).
	struct EnvironmentMapDesc
	{
		EnvironmentMapDesc() = default;

		EnvironmentMapDesc(
			TextureAssetHandle irr,
			TextureAssetHandle pre,
			TextureAssetHandle brdf) : irradiance(irr), prefilter(pre), brdfLut(brdf)
		{}

		EnvironmentMapDesc(EnvironmentMapDesc&&) noexcept = default;
		EnvironmentMapDesc(const EnvironmentMapDesc&)     = delete;

		EnvironmentMapDesc&
		operator=(EnvironmentMapDesc&&) noexcept = default;

		EnvironmentMapDesc&
		operator=(const EnvironmentMapDesc&) = delete;

		TextureAssetHandle irradiance;
		TextureAssetHandle prefilter;
		TextureAssetHandle brdfLut;
	};

	struct PbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;

		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		// Optional material maps, from AddTextureAsset.
		TextureAssetHandle baseColorTexture;
		TextureAssetHandle normalTexture;
		TextureAssetHandle ormTexture;
	};

	struct ChannelRouteDesc
	{
		TextureAssetHandle texture;
		uint16_t           channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};

	struct LoosePbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;

		// Cutout; see PbrMaterialDesc. A loose material routes its alpha explicitly (baseColor[3]),
		// so unlike a baked one it can always sample a real alpha channel.
		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		std::array<ChannelRouteDesc, 4> baseColor;  // R, G, B, A
		std::array<ChannelRouteDesc, 3> orm;        // AO, roughness, metallic
		std::array<ChannelRouteDesc, 2> normal;     // X, Y (Z reconstructed in shader)
	};

	class BGL_API IScene : public core::Ref
	{
	public:
		IScene(IScene&&) noexcept      = delete;
		IScene(const IScene&) noexcept = delete;

		IScene&
		operator=(IScene&&) noexcept = delete;

		IScene&
		operator=(const IScene&) noexcept = delete;

		virtual const SceneDesc&
		GetDesc() const noexcept = 0;

		virtual GeomHandle
		AddCubeGeom(MaterialHandle material = {}) = 0;

		/**
		 * Adds a procedurally generated UV sphere of `radius`, centred on the origin, as static-mesh
		 * geometry.
		 *
		 * @throws SceneError if either segment count is 0, or a buffer allocation fails.
		 */
		virtual GeomHandle
		AddSphereGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          radius,
			MaterialHandle material = {}) = 0;

		/**
		 * Adds a procedurally generated plane as static-mesh geometry: a flat `width` x `height` quad
		 * centred on the origin, subdivided into an `xSegments` x `ySegments` grid.
		 * @throws SceneError if either segment count is 0, if the grid needs more meshlets than one
		 *         dispatch can launch, or if a buffer allocation fails.
		 */
		virtual GeomHandle
		AddPlaneGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          width,
			float          height,
			MaterialHandle material = {}) = 0;

		/**
		 * Adds one mesh of a loaded BMesh as static-mesh geometry, uploading its submeshes'
		 * vertex / index / meshlet data into this scene's buffers. Each submesh is bound to
		 * `materials[submesh.material]`; a submesh whose material index is out of range (e.g. the
		 * source had none) is left unlit.
		 *
		 * @param mesh       A BMesh loaded from disk (see assetlib::load).
		 * @param meshIndex  Index into `mesh.meshes`.
		 * @param materials  Materials parallel to `mesh.materials`, resolved by the caller.
		 * @throws SceneError if `meshIndex` is out of range or a buffer allocation fails.
		 */
		virtual GeomHandle
		AddStaticMesh(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials) = 0;

		virtual TextureAssetHandle
		AddTextureAsset(assetlib::ImageData img, std::string debugName = "") = 0;

		/**
		 * Destroys a texture asset, releasing its GPU resource and its bindless descriptor slot.
		 * The release is deferred until the frames that could still be sampling it have completed.
		 *
		 * The scene does not know which materials sample a texture. Deleting one that a live
		 * material still routes leaves that material reading a slot that a later AddTextureAsset
		 * may reuse; delete such materials first.
		 *
		 * @param texture A handle returned by AddTextureAsset.
		 * @throws SceneError if the handle is null, or already deleted.
		 */
		virtual void
		DeleteTextureAsset(TextureAssetHandle texture) = 0;

		/**
		 * Creates a PBR material in this scene's material buffer and returns a handle
		 * referencing it. Pass the handle to a geometry-creating method to bind it.
		 */
		virtual MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) = 0;

		/**
		 * Creates a loose (unbaked, per-channel) PBR material in this scene's loose-material buffer
		 * and returns a handle referencing it. Bind it like any material; it renders through the same
		 * lighting path as a PbrMaterial. See LoosePbrMaterialDesc.
		 */
		virtual MaterialHandle
		CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc) = 0;

		/**
		 * Destroys a material created by CreatePbrMaterial or CreateLoosePbrMaterial, freeing its
		 * slot in the corresponding material buffer.
		 *
		 * A submesh stores the material's slot index, not a generation-checked handle, so a submesh
		 * still bound to a deleted material silently picks up whichever material next takes that
		 * slot. Rebind every submesh using it (SetSubmeshMaterial) before deleting.
		 *
		 * @param material A handle returned by a material-creating method.
		 * @throws SceneError if the handle is invalid, already deleted, or names a material type
		 *         that has no storage to free (kNull, kAssert).
		 */
		virtual void
		DeleteMaterial(MaterialHandle material) = 0;

		/**
		 * Rebinds the material of one submesh of a geom. Because a geom's submeshes are shared by
		 * every instance placed from it, this changes the material (and its PSO bucket) for all of
		 * them. `submeshIndex` is relative to the geom's submesh range.
		 *
		 * @throws SceneError if the geom handle is invalid, the material is invalid, or the submesh
		 *         index is out of range.
		 */
		virtual void
		SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material) = 0;

		/**
		 * Removes geometry and frees its underlying vertex/index/meshlet data.
		 *
		 * @param geom A handle returned by a geometry-creating method.
		 * @throws SceneError if the handle is invalid, already removed, or still
		 *         referenced by one or more live mesh instances (held by a SceneView).
		 */
		virtual void
		DeleteGeom(GeomHandle geom) = 0;

	protected:
		IScene() noexcept = default;
	};

	using SceneHandle = core::SharedRef<IScene>;
}

template class BGL_API core::SharedRef<bgl::IScene>;
