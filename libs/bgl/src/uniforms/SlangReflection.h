#pragma once
#include "uniforms/ReflectedLayout.h"

namespace bgl
{
	// Walks a slang constant-buffer type layout into the API-agnostic ReflectedLayout
	// tree. This is the one place that reads slang reflection; everything downstream
	// (Uniforms, the shader cache) works off the POD result.
	ReflectedLayout
	ReflectLayoutFromSlang(slang::TypeLayoutReflection* typeLayout);
}
