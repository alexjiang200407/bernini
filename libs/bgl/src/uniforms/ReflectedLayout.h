#pragma once
#include "uniforms/UniformValueType.h"

namespace bgl
{
	enum class HandleKind : uint8_t
	{
		kNone,
		kBuffer,
		kTexture,
		kSampler,
	};

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

		// Which kind of bindless handle this is, when it is one -- they share a valueType, so the
		// declared type is the only thing that tells them apart. A backend that resolves a handle at
		// bind time needs it: on Metal a buffer becomes a gpuAddress and a texture or sampler an
		// MTLResourceID, read from different pools. Always kNone on D3D12, which reaches every
		// resource through one descriptor heap and so needs no distinction.
		HandleKind                   handleKind = HandleKind::kNone;
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
