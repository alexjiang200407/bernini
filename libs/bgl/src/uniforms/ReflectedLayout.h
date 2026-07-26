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

		// A bindless handle, not a plain uint2 (they share a valueType). Marks a resource the backend
		// resolves at dispatch: Metal to a gpuAddress, WebGPU to a bind-group entry (see `binding`).
		// Always false on D3D12, which reaches resources through the descriptor heap.
		bool                         isResourceHandle = false;
		std::vector<ReflectedField>  fields;   // kStruct members
		std::vector<ReflectedLayout> element;  // kArray element type (0 or 1 entry)
	};

	struct ReflectedField
	{
		std::string     name;
		uint32_t        offset = 0;
		ReflectedLayout layout;

		// The WGSL (group, binding) a resource leaf binds to; used only by the WebGPU backend.
		uint32_t group   = 0xFFFFFFFF;
		uint32_t binding = 0xFFFFFFFF;
	};
}
