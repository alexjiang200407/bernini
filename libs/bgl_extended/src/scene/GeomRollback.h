#pragma once
#include <bgl_common/idl/RawRange.h>
#include <core/containers/multi_slot_handle.h>
#include <functional>
#include <vector>

namespace bgl
{
	/**
	 * Hands back the ranges of a geom that failed to build.
	 *
	 * Every Add() is registered here and released on the way out, unless Commit() says the geom was
	 * built and now owns them.
	 */
	class GeomRollback
	{
	public:
		GeomRollback() = default;

		GeomRollback(const GeomRollback&) = delete;
		GeomRollback&
		operator=(const GeomRollback&) = delete;

		// Passes `handle` straight back, so an Add() can be wrapped where it stands.
		template <typename Buffer>
		core::multi_slot_handle
		Track(Buffer& buffer, core::multi_slot_handle handle)
		{
			m_Undo.emplace_back([&buffer, handle]() { buffer.Erase(handle); });
			return handle;
		}

		// The raw arena's counterpart: it hands back a byte offset rather than a slot handle,
		// and frees by the same.
		template <typename Buffer>
		idl::RawRange
		Track(Buffer& buffer, idl::RawRange range)
		{
			m_Undo.emplace_back([&buffer, range]() { buffer.Erase(range.byteStart); });
			return range;
		}

		void
		Commit() noexcept
		{
			m_Undo.clear();
		}

		~GeomRollback()
		{
			// Newest first, so no range is freed before one allocated after it.
			for (auto undo = m_Undo.rbegin(); undo != m_Undo.rend(); ++undo)
			{
				try
				{
					(*undo)();
				}
				catch (...)
				{
					// Already unwinding the failure that matters; a failed undo must not replace it.
				}
			}
		}

	private:
		std::vector<std::function<void()>> m_Undo;
	};
}
