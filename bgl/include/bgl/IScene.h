#pragma once
#include <bgl/util.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct SceneDesc
	{
		uint32_t maxInstances = 0;
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

	protected:
		IScene() = default;
	};

	using SceneHandle = core::SharedRef<IScene>;

	template class BGL_API core::SharedRef<IScene>;
}
