#pragma once
#include "uniforms/UniformLayoutEntry.h"

#include <core/str/str.h>

namespace bgl
{
	// Where each bindless handle sits in a cbuffer mirror, and which pool resolves it.
	struct HandleSlot
	{
		uint32_t   offset = 0;
		HandleKind kind   = HandleKind::kNone;
	};

	using MetalHandleOffsetMap = core::str::unordered_str_map<std::vector<HandleSlot>>;

	// Reflects a linked program's constant buffers into the API-agnostic UniformLayoutMap plus a
	// side table of each cbuffer's bindless-handle offsets. Recomputes the byte layout itself, since
	// Metal reflection is blind to resource handles in the ordinary-data category (see the .cpp).
	// Shared by the compute and meshlet pipelines.
	void
	ReflectCbuffers(
		slang::ProgramLayout* layout,
		UniformLayoutMap&     outEntries,
		MetalHandleOffsetMap& outHandleOffsets);
}
