#pragma once
#include <bgl/api.h>
#include <bgpu/resource/Buffer.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * Another owner's buffer adopted into the renderer's resource manager by IGraphics::ImportBuffer,
	 * read-only. Releasing the last reference destroys the renderer's view once every frame that may
	 * read it has retired; the memory itself is the exporter's.
	 */
	class BGL_API IExternalBuffer : public core::Ref
	{
	public:
		IExternalBuffer(IExternalBuffer&&) noexcept      = delete;
		IExternalBuffer(const IExternalBuffer&) noexcept = delete;

		IExternalBuffer&
		operator=(IExternalBuffer&&) noexcept = delete;

		IExternalBuffer&
		operator=(const IExternalBuffer&) noexcept = delete;

		/** What an instance writer's parameters bind to reach it. Valid for this object's life. */
		[[nodiscard]] virtual bgpu::BufferHandle
		GetHandle() const noexcept = 0;

	protected:
		IExternalBuffer() noexcept = default;
	};

	using ExternalBufferRef = core::SharedRef<IExternalBuffer>;
}

template class BGL_API core::SharedRef<bgl::IExternalBuffer>;
