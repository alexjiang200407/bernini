#pragma once
#include <bgl/api.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * A caller's kernel, compiled against the renderer's instance block: what
	 * ISceneView::SetBlockWriter binds to a block. One writer may place any number of blocks,
	 * in any view of the graphics that created it, each with parameters of its own. Released
	 * before that graphics, as its views are: it holds a pipeline on the graphics' device.
	 */
	class BGL_API IMeshInstanceWriter : public core::Ref
	{
	public:
		IMeshInstanceWriter(IMeshInstanceWriter&&) noexcept      = delete;
		IMeshInstanceWriter(const IMeshInstanceWriter&) noexcept = delete;

		IMeshInstanceWriter&
		operator=(IMeshInstanceWriter&&) noexcept = delete;

		IMeshInstanceWriter&
		operator=(const IMeshInstanceWriter&) noexcept = delete;

		[[nodiscard]] virtual const MeshInstanceWriterDesc&
		GetDesc() const noexcept = 0;

	protected:
		IMeshInstanceWriter() noexcept = default;
	};

	using MeshInstanceWriterRef = core::SharedRef<IMeshInstanceWriter>;
}

template class BGL_API core::SharedRef<bgl::IMeshInstanceWriter>;
