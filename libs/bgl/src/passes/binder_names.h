#pragma once
#include "pipeline/MeshletKernel.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	/** The `uniformKey` of every binding in a `SceneBuffer`-shaped table, in order. */
	template <typename Bindings>
	std::vector<std::string_view>
	UniformKeys(const Bindings& bindings)
	{
		std::vector<std::string_view> keys;
		keys.reserve(bindings.size());

		for (const auto& binding : bindings)
		{
			keys.push_back(binding.uniformKey);
		}

		return keys;
	}

	/**
	 * Resolves the names a pass binds against its whole PSO family at once, and fatals on any that
	 * no variant declares.
	 *
	 * A variant omitting a member is ordinary and stays silent; a name *no* variant declares is a
	 * typo or a shader rename, which binding cannot report because `IsValid()` reads the same either
	 * way. @pre call it once from the pass's `Init`, never per draw.
	 *
	 * @param binder  The pass, for the message.
	 * @param kernels One family's kernels. A kernel that does not declare `cbuffer` costs nothing.
	 */
	inline void
	ValidateBinderNames(
		std::string_view                  binder,
		std::span<const MeshletKernel>    kernels,
		std::string_view                  cbuffer,
		std::span<const std::string_view> names)
	{
		std::vector<const Uniforms*> variants;
		variants.reserve(kernels.size());

		for (const MeshletKernel& kernel : kernels)
		{
			const auto found = kernel.uniforms.find(cbuffer);
			variants.push_back(found != kernel.uniforms.end() ? &found->second : nullptr);
		}

		const std::vector<std::string_view> unknown = FindUnknownMembers(variants, names);
		if (unknown.empty())
		{
			return;
		}

		std::string joined;
		for (const std::string_view name : unknown)
		{
			joined += joined.empty() ? "" : ", ";
			joined += name;
		}

		gfatal("{} binds '{}' members no PSO declares: {}", binder, cbuffer, joined);
	}
}
