#pragma once
#include "ErrorChecker.h"
#include "cmd/CommandAllocator.h"

namespace bgl
{
	class CommandAllocatorImpl
	{
	public:
		CommandAllocatorImpl(wrl::ComPtr<ID3D12CommandAllocator> commandAllocator) :
			m_CommandAllocator(std::move(commandAllocator))
		{}

		ID3D12CommandAllocator*
		Get() const
		{
			return m_CommandAllocator.Get();
		}

		void
		Reset();

	private:
		wrl::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
		friend class DeviceImpl;
	};
}
