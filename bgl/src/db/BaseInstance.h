#pragma once
#include "db/Entry.h"
#include <bgl/PsoType.h>

namespace bgl::db
{
	struct BaseInstance
	{
		PsoType psoType;
		Entry   meshInstance;
		Entry   materialInstance;
	};
}
