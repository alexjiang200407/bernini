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
	using MetalStageBindingMap = core::str::unordered_str_map<uint32_t>;

	// Reflects a linked program's constant buffers into the API-agnostic UniformLayoutMap plus a
	// side table of each cbuffer's bindless-handle offsets. Recomputes the byte layout itself, since
	// Metal reflection is blind to resource handles in the ordinary-data category (see the .cpp).
	// Shared by the compute and meshlet pipelines.
	void
	ReflectCbuffers(
		slang::ProgramLayout* layout,
		UniformLayoutMap&     outEntries,
		MetalHandleOffsetMap& outHandleOffsets);

	// The [[buffer(N)]] index of each cbuffer in one compiled stage. Each stage is compiled as its
	// own program (see MeshletPipeline_metal), so indices are per-stage: the same cbuffer can sit at
	// different N in two stages, and two different cbuffers can share an N across stages. A cbuffer
	// must therefore be bound per stage at that stage's index, never at one shared index.
	void
	ReflectStageBindings(slang::ProgramLayout* layout, MetalStageBindingMap& outBindings);
}
