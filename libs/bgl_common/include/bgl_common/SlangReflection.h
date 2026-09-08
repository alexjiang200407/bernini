#pragma once
#include <bgl_common/ReflectedLayout.h>
#include <bgl_common/UniformValueType.h>

#include <slang.h>
#include <string>
#include <vector>

namespace bgl
{
	// Walks a slang constant-buffer type layout into the API-agnostic ReflectedLayout
	// tree. Everything downstream (Uniforms, the shader cache) works off the POD result.
	ReflectedLayout
	ReflectLayoutFromSlang(slang::TypeLayoutReflection* typeLayout);

	// Every struct a module declares, namespaces walked into. bgl_idlgen keeps a copy of this walk
	// rather than calling it: the generator produces the headers this library is compiled against,
	// so it is built first and links none of it.
	void
	CollectStructDecls(slang::DeclReflection* decl, std::vector<slang::DeclReflection*>& out);

	// A type's name as its module spells it, which is what findTypeByName takes back.
	std::string
	FullTypeName(slang::TypeReflection* type);

	// How a struct lays out as the element of a buffer -- the arena's rules, not a constant
	// buffer's. Null when the type does not resolve as one. The rules belong to the layout's
	// target and the targets disagree; see bgl_common/SurfaceReflection.h.
	slang::TypeLayoutReflection*
	BufferElementLayout(slang::ProgramLayout* layout, slang::TypeReflection* type);

	// Maps a scalar, vector, or 4x4 matrix type to its UniformValueType.
	UniformValueType
	ResolveSlangValueType(slang::TypeReflection* type);
}
