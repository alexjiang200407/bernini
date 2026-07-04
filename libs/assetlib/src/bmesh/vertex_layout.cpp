#include <assetlib/bmesh/VertexLayout.h>

namespace assetlib::bmesh
{
	uint32_t
	formatSize(VertexFormat format) noexcept
	{
		switch (format)
		{
		case VertexFormat::kFloat32x2:
			return 8;
		case VertexFormat::kFloat32x3:
			return 12;
		case VertexFormat::kFloat32x4:
			return 16;
		case VertexFormat::kUnorm8x4:
			return 4;
		case VertexFormat::kUnorm16x2:
			return 4;
		case VertexFormat::kUnorm16x4:
			return 8;
		case VertexFormat::kUint16x4:
			return 8;
		}
		return 0;
	}
}
