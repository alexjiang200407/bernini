#pragma once

#include <directx/d3d12.h>

namespace bgpu
{
	/**
	 * NVIDIA's driver asked to hold a device's GPU at its maximum clock, through NvAPI's Reflex
	 * entry point with only its low-latency boost on. Any other GPU, a driver without Reflex or no
	 * NVIDIA driver at all is logged and leaves Granted() false. Holds NvAPI loaded while it lives.
	 */
	class D3d12ClockBoost
	{
	public:
		explicit D3d12ClockBoost(ID3D12Device* device) noexcept;
		~D3d12ClockBoost() noexcept;

		D3d12ClockBoost(const D3d12ClockBoost&) = delete;
		D3d12ClockBoost(D3d12ClockBoost&&)      = delete;
		D3d12ClockBoost&
		operator=(const D3d12ClockBoost&) = delete;
		D3d12ClockBoost&
		operator=(D3d12ClockBoost&&) = delete;

		[[nodiscard]] bool
		Granted() const noexcept
		{
			return m_Granted;
		}

	private:
		bool m_Loaded  = false;
		bool m_Granted = false;
	};
}
