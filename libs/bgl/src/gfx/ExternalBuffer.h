#pragma once
#include <bgl/IExternalBuffer.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	class ExternalBuffer final : public core::RefCounter<IExternalBuffer>
	{
	public:
		ExternalBuffer(
			bgpu::ResourceManagerRef resourceManager,
			bgpu::BufferHandle       handle) noexcept;

		~ExternalBuffer() noexcept override;

		ExternalBuffer(ExternalBuffer&&) noexcept      = delete;
		ExternalBuffer(const ExternalBuffer&) noexcept = delete;

		ExternalBuffer&
		operator=(ExternalBuffer&&) noexcept = delete;

		ExternalBuffer&
		operator=(const ExternalBuffer&) noexcept = delete;

		[[nodiscard]] bgpu::BufferHandle
		GetHandle() const noexcept override
		{
			return m_Handle;
		}

	private:
		bgpu::ResourceManagerRef m_ResourceManager;
		bgpu::BufferHandle       m_Handle;
	};
}
