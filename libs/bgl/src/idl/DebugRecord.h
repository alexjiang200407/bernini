// THIS IS A FILE GENERATED FROM DebugRecord.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct DebugRecord
	{
		uint32_t errcode;
		uint32_t value;
		uint32_t limit;
		uint32_t context;
	};

	static_assert(sizeof(DebugRecord) == 16);
	static_assert(offsetof(DebugRecord, errcode) == 0);
	static_assert(offsetof(DebugRecord, value) == 4);
	static_assert(offsetof(DebugRecord, limit) == 8);
	static_assert(offsetof(DebugRecord, context) == 12);

}
