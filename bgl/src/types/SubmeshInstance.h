#pragma once
#include "idl/Entry.h"
#include <bgl/PsoType.h>

namespace bgl
{
	struct SubmeshInstance
	{
		PsoType    psoType;
		idl::Entry meshInstance;
		uint32_t   submeshIndex;
	};
}
