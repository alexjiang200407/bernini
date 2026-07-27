#pragma once
#include "uniforms/ReflectedLayout.h"

namespace bgl
{
	// Walks a slang constant-buffer type layout into the API-agnostic ReflectedLayout
	// tree. Everything downstream (Uniforms, the shader cache) works off the POD result.
	ReflectedLayout
	ReflectLayoutFromSlang(slang::TypeLayoutReflection* typeLayout);

	// Maps a scalar, vector, or 4x4 matrix type to its UniformValueType. Shared with the WGSL
	// reflection path, which builds its own tree (bindings, not a descriptor heap) but classifies
	// plain-data leaves identically.
	UniformValueType
	ResolveSlangValueType(slang::TypeReflection* type);
}
