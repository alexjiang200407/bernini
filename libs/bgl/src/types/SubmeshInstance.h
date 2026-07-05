#pragma once
#include "idl/Entry.h"

namespace bgl
{
	struct SubmeshInstance
	{
		idl::Entry meshInstance;
		uint32_t   submeshIndex;
	};
}
