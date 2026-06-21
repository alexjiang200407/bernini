#pragma once
#include <bgl/GeomType.h>
#include <bgl/MaterialType.h>
#include <bgl/PsoType.h>
#include <bgl/util.h>
#include <core/containers/slot_handle.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <bgl/glm.h>

namespace bgl
{
	struct MeshInstanceHandle
	{
		PsoType           psoType = PsoType::kInvalid;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const
		{
			return psoType != PsoType::kInvalid;
		}
	};

	struct GeomHandle
	{
		GeomType          geomType = GeomType::kInvalid;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const
		{
			return geomType != GeomType::kInvalid;
		}
	};

	struct MaterialHandle
	{
		MaterialType      materialType;
		core::slot_handle handle;

		[[nodiscard]]
		bool
		IsValid() const
		{
			return materialType != MaterialType::kInvalid;
		}
	};

	struct SceneDesc
	{
		uint32_t maxInstances = 0;
		uint32_t maxGeom      = 0;
		uint32_t maxMeshlets  = 0;
		uint32_t maxVertices  = 0;
		uint32_t maxIndices   = 0;
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
		GetDesc() const = 0;

		virtual GeomHandle
		AddCubeGeom() = 0;

		virtual MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, MaterialHandle material, glm::mat4 transform) = 0;

	protected:
		IScene() = default;
	};

	using SceneHandle = core::SharedRef<IScene>;

	template class BGL_API core::SharedRef<IScene>;
}
