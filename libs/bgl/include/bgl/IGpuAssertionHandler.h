#pragma once

namespace bgl
{
	struct GpuAssertionReport
	{
		uint32_t        raisedCount  = 0;
		bool            overflow     = false;
		const uint32_t* errcodes     = nullptr;
		uint32_t        errcodeCount = 0;
	};

	class IGpuAssertionHandler
	{
	public:
		virtual ~IGpuAssertionHandler() = default;

		virtual void
		OnGpuAssertion(const GpuAssertionReport& report) noexcept = 0;
	};
}
