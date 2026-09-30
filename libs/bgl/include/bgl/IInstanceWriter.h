#pragma once
#include <bgl/api.h>
#include <bgl/types/InstanceWriterDesc.h>
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
	class BGL_API IInstanceWriter : public core::Ref
	{
	public:
		IInstanceWriter(IInstanceWriter&&) noexcept      = delete;
		IInstanceWriter(const IInstanceWriter&) noexcept = delete;

		IInstanceWriter&
		operator=(IInstanceWriter&&) noexcept = delete;

		IInstanceWriter&
		operator=(const IInstanceWriter&) noexcept = delete;

		[[nodiscard]] virtual const InstanceWriterDesc&
		GetDesc() const noexcept = 0;

	protected:
		IInstanceWriter() noexcept = default;
	};

	using InstanceWriterRef = core::SharedRef<IInstanceWriter>;
}

template class BGL_API core::SharedRef<bgl::IInstanceWriter>;
