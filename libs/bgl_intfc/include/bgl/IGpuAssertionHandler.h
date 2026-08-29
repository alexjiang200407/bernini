#pragma once

namespace bgl
{
	struct GpuAssertionReport
	{
		// How many assertions the GPU raised this frame; may exceed errcodes.size() when overflow is
		// set and the debug ring buffer dropped records.
		uint32_t raisedCount = 0;
		bool     overflow    = false;

		// The captured error codes. Valid only for the duration of the OnGpuAssertion call; copy the
		// contents to retain them.
		std::span<const uint32_t> errcodes;
	};

	class IGpuAssertionHandler
	{
	public:
		virtual ~IGpuAssertionHandler() = default;

		virtual void
		OnGpuAssertion(const GpuAssertionReport& report) noexcept = 0;
	};
}
