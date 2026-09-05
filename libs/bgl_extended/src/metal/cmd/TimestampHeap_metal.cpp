#include "cmd/TimestampHeap_metal.h"

#include "cmd/TimestampHeap.h"
#include <bgl_common/gassert.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace bgl
{
	TimestampHeap::TimestampHeap(
		NS::SharedPtr<MTL::CounterSampleBuffer> buffer,
		uint32_t capacity) noexcept : m_Buffer(std::move(buffer)), m_Capacity(capacity)
	{
		gassert(m_Buffer.get() != nullptr, "A timestamp heap needs a counter sample buffer");
	}

	void
	TimestampHeap::Read(uint32_t first, std::span<uint64_t> out) const noexcept
	{
		gassert(first + out.size() <= m_Capacity, "Timestamp read outside the heap");

		for (uint64_t& v : out)
		{
			v = c_UnwrittenTimestamp;
		}

		if (out.empty())
		{
			return;
		}

		// resolveCounterRange autoreleases its NS::Data.
		auto pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		NS::Data* data = m_Buffer->resolveCounterRange(NS::Range::Make(first, out.size()));
		if (data == nullptr)
		{
			return;
		}

		const auto*  results = static_cast<const MTL::CounterResultTimestamp*>(data->bytes());
		const size_t count   = data->length() / sizeof(MTL::CounterResultTimestamp);
		for (size_t i = 0; i < count && i < out.size(); ++i)
		{
			const uint64_t sample = results[i].timestamp;
			out[i] = sample == MTL::CounterErrorValue ? c_UnwrittenTimestamp : sample;
		}
	}
}
