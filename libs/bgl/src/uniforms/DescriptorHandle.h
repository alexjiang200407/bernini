#pragma once

namespace bgl
{
	class DescriptorHandle
	{
	public:
		explicit DescriptorHandle(uint32_t hi, uint32_t lo) : m_Hi(hi), m_Lo(lo) {}
		explicit DescriptorHandle(uint32_t hi) : m_Hi(hi) {}

	private:
		uint32_t m_Hi = 0;
		uint32_t m_Lo = 0;
	};
}
