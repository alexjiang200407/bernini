#pragma once
#include "D3d12ErrorChecker.h"
#include <bgpu/cmd/CommandAllocator.h>
#include <core/err/util.h>
#include <spdlog/spdlog.h>

namespace bgpu
{
	class CommandAllocator : public core::RefCounter<ICommandAllocator>
	{
	public:
		CommandAllocator(const CommandAllocator&) noexcept = delete;
		CommandAllocator(CommandAllocator&&) noexcept      = delete;
		~CommandAllocator() noexcept override { spdlog::trace("~CommandAllocator"); }

		CommandAllocator&
		operator=(const CommandAllocator&) noexcept = delete;

		CommandAllocator&
		operator=(CommandAllocator&&) noexcept = delete;

		CommandAllocator(wrl::ComPtr<ID3D12CommandAllocator> commandAllocator) :
			m_CommandAllocator(std::move(commandAllocator))
		{
			core::ensure(m_CommandAllocator != nullptr, "Command allocator cannot be null");
		}

		ID3D12CommandAllocator*
		GetD3D12CommandAllocator() const noexcept
		{
			return m_CommandAllocator.Get();
		}

		void
		ResetAllocator() noexcept override;

	private:
		wrl::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	};
}
