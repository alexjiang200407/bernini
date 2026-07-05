#pragma once
#include <bgl/GeomType.h>
#include <bgl/MaterialType.h>
#include <bgl/PsoType.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/util.h>
#include <assetlib_structs/ImageData.h>
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

	struct MeshInstanceHandle
	{
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return !handle.is_null();
		}
	};

	struct GeomHandle
	{
		GeomType          geomType = GeomType::kInvalid;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return geomType != GeomType::kInvalid;
		}
	};

	struct MaterialHandle
	{
		MaterialType      materialType = MaterialType::kInvalid;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const noexcept
		{
			return materialType != MaterialType::kInvalid;
		}
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

	// Decoded IBL images (two cube maps + the 2D BRDF LUT). Callers decode the files
	// themselves (e.g. assetlib::loadDDS) so bgl stays codec-free; the engine only
	// consumes the pixel data.
	struct EnvironmentMapDesc
	{
		EnvironmentMapDesc() = default;

		// Lets callers brace-init `{ loadDDS(a), loadDDS(b), loadDDS(c) }` even though the
		// move-only ImageData members make this a non-aggregate.
		EnvironmentMapDesc(
			assetlib::ImageData irr,
			assetlib::ImageData pre,
			assetlib::ImageData brdf) :
			irradiance(std::move(irr)), prefilter(std::move(pre)), brdfLut(std::move(brdf))
		{}

		// Move-only, mirroring ImageData (silences C4625/C4626 under strict warnings).
		EnvironmentMapDesc(EnvironmentMapDesc&&) noexcept = default;
		EnvironmentMapDesc(const EnvironmentMapDesc&)     = delete;
		EnvironmentMapDesc&
		operator=(EnvironmentMapDesc&&) noexcept = default;
		EnvironmentMapDesc&
		operator=(const EnvironmentMapDesc&) = delete;

		assetlib::ImageData irradiance;
		assetlib::ImageData prefilter;
		assetlib::ImageData brdfLut;
	};

	struct PbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;
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
		 * Creates a PBR material in this scene's material buffer and returns a handle
		 * referencing it. Pass the handle to a geometry-creating method to bind it.
		 */
		virtual MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) = 0;

		/**
		 * Loads the three precomputed IBL .dds maps and binds them as this scene's
		 * environment for the PBR pass. Replaces any previously set environment.
		 *
		 * @throws SceneError if any file cannot be loaded.
		 */
		virtual void
		SetEnvironmentMap(const EnvironmentMapDesc& desc) = 0;

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

	template class BGL_API core::SharedRef<IScene>;
}
