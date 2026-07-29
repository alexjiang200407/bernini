#pragma once
#include "uniforms/UniformValueType.h"

namespace bgl
{
	struct ReflectedField;

	// What a resource leaf binds to on a backend without bindless: WebGPU turns it into a bind-group
	// entry of the matching kind (see ReflectedField::binding), Metal into a gpuAddress. kNone on
	// D3D12, which reaches every resource through the descriptor heap, and on any plain-data leaf.
	enum class ResourceBinding : uint8_t
	{
		kNone,
		kBuffer,
		kTexture,
		kSampler
	};

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

		// Non-kNone marks a bindless handle rather than a plain uint2 -- they share a valueType.
		ResourceBinding              resourceBinding = ResourceBinding::kNone;
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
