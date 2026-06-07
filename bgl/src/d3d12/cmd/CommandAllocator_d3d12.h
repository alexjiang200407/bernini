#pragma once
#include "ErrorChecker.h"
#include "cmd/CommandAllocator.h"

namespace bgl
{
	class CommandAllocator : public core::RefCounter<ICommandAllocator>
	{
	public:
		CommandAllocator(wrl::ComPtr<ID3D12CommandAllocator> commandAllocator) :
			m_CommandAllocator(std::move(commandAllocator))
		{}

		ID3D12CommandAllocator*
		GetD3D12CommandAllocator() const
		{
			return m_CommandAllocator.Get();
		}

		void
		ResetAllocator() override;

	private:
		wrl::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	};
}
