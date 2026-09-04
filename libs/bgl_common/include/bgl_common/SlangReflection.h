#pragma once
#include <bgl_common/ReflectedLayout.h>
#include <bgl_common/UniformValueType.h>

#include <slang.h>

namespace bgl
{
	// Walks a slang constant-buffer type layout into the API-agnostic ReflectedLayout
	// tree. Everything downstream (Uniforms, the shader cache) works off the POD result.
	ReflectedLayout
	ReflectLayoutFromSlang(slang::TypeLayoutReflection* typeLayout);

	// Maps a scalar, vector, or 4x4 matrix type to its UniformValueType.
	UniformValueType
	ResolveSlangValueType(slang::TypeReflection* type);
}
