#pragma once
#include <bgl/api.h>
#include <stdexcept>

namespace bgl
{
	class BGL_API ApiError : public std::runtime_error
	{
	public:
		ApiError() = delete;
		using std::runtime_error::runtime_error;
	};
}
