#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
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

		// Optional material maps, from AddTextureAsset.
		TextureAssetHandle baseColorTexture;
		TextureAssetHandle normalTexture;
		TextureAssetHandle ormTexture;
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

		virtual GeomHandle
		AddSphereGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          radius,
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
		 * Creates a PBR material in this scene's material buffer and returns a handle
		 * referencing it. Pass the handle to a geometry-creating method to bind it.
		 */
		virtual MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) = 0;

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
