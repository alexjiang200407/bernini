// The whole point of this translation unit is what it does NOT link. It compiles every bgl_common
// header against bgl_common alone, so one that reaches into libs/bgl_extended/src stops the build
// here rather than when a second renderer is written -- the same construction, and the same reason,
// as libs/bgl/src/selfcheck.cpp one tier down.
//
// It is what a renderer's own build cannot check: bgl_extended compiles these headers with
// libs/bgl_extended/src on the include path, so a leak resolves there and stays invisible until
// something else includes them.
//
// The list is by hand because a header nothing includes is exactly the one that rots. Add a public
// header, add it here.
#include <bgl_common/ReflectedLayout.h>
#include <bgl_common/SlangReflection.h>
#include <bgl_common/UniformMirror.h>
#include <bgl_common/UniformValueType.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/idl.h>
#include <bgl_common/shadercache/util.h>

namespace
{
	// Without a definition the archive is empty, which some toolchains warn on.
	[[maybe_unused]] const bgl::ReflectedLayout c_Unused;
}
