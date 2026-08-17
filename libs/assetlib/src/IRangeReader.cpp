#include "IRangeReader.h"

#include <core/err/util.h>

namespace assetlib
{
	void
	IRangeReader::CheckRange(uint64_t bytes, uint64_t offset) const
	{
		const uint64_t size = GetSize();
		core::throw_runtime_error_if(
			bytes > size || offset > size - bytes,
			"{}: a range extends past the end of the source",
			m_What);
	}
}
