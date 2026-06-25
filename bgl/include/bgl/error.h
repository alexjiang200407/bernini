#pragma once
#include <bgl/util.h>

namespace bgl
{
	class BGL_API ApiError : public std::runtime_error
	{
	public:
		ApiError() = delete;
		using std::runtime_error::runtime_error;
	};
}
