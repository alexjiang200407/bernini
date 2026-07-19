#pragma once
#include "uniforms/UniformValueType.h"

namespace bgl
{
	struct ReflectedField;

	// Serializable mirror of one constant-buffer member tree, decoupled from any
	// graphics API's live reflection object. It carries exactly what Uniforms needs to
	// lay out the CPU buffer, so it can be built once from shader reflection and then
	// cached to disk (see the shader cache) instead of re-reflecting the source.
	struct ReflectedLayout
	{
		UniformType      kind        = UniformType::kNull;
		UniformValueType valueType   = UniformValueType::kNone;
		uint32_t         size        = 0;
		uint32_t         arrayCount  = 0;
		uint32_t         arrayStride = 0;

		// A bindless handle, not a plain uint2 (they share a valueType). Metal-only: it marks the
		// fields the backend translates to a gpuAddress at dispatch. Always false on D3D12.
		bool                         isResourceHandle = false;
		std::vector<ReflectedField>  fields;   // kStruct members
		std::vector<ReflectedLayout> element;  // kArray element type (0 or 1 entry)
	};

	struct ReflectedField
	{
		std::string     name;
		uint32_t        offset = 0;
		ReflectedLayout layout;
	};
}
