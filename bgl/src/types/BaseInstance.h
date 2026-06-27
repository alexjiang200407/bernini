#pragma once
#include "idl/Entry.h"
#include <bgl/PsoType.h>

namespace bgl
{
	struct BaseInstance
	{
		PsoType    psoType;
		idl::Entry meshInstance;
		idl::Entry materialInstance;
	};
}
