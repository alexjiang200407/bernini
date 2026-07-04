#pragma once
#include <bgl/GeomType.h>
#include <bgl/MaterialType.h>
#include <bgl/PsoType.h>
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
		AddCubeGeom() = 0;

		virtual GeomHandle
		AddSphereGeom(uint32_t xSegments, uint32_t ySegments, float radius) = 0;

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
