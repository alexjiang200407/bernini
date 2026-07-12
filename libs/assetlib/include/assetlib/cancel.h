#pragma once

namespace assetlib
{
	class Cancelled : public std::exception
	{
	public:
		[[nodiscard]] const char*
		what() const noexcept override
		{
			return "assetlib: the operation was cancelled";
		}
	};

	using CancelToken = std::stop_token;

	/** @throws Cancelled if `cancel` has been signalled. */
	inline void
	throwIfCancelled(const CancelToken& cancel)
	{
		if (cancel.stop_requested())
			throw Cancelled();
	}
}
