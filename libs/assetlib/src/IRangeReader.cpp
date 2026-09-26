#include "IRangeReader.h"

#include <core/err/util.h>
#include <cstdint>

namespace assetlib
{
	void
	IRangeReader::CheckRange(uint64_t bytes, uint64_t offset) const
	{
		const uint64_t size = GetSize();
		if (bytes > size || offset > size - bytes)
		{
			core::throw_runtime_error("{}: a range extends past the end of the source", m_What);
		}
	}
}
