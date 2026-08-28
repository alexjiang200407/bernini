#include "passes/BinderNames.h"

namespace bgl
{
	BinderNames&
	BinderNames::Check(std::string_view cbuffer, std::span<const std::string_view> names)
	{
		std::vector<const Uniforms*> variants;
		variants.reserve(m_Kernels.size());

		for (const MeshletKernel& kernel : m_Kernels)
		{
			const auto found = kernel.uniforms.find(cbuffer);
			variants.push_back(found != kernel.uniforms.end() ? &found->second : nullptr);
		}

		const std::vector<std::string_view> unknown = FindUnknownMembers(variants, names);
		if (unknown.empty())
		{
			return *this;
		}

		std::string joined;
		for (const std::string_view name : unknown)
		{
			joined += joined.empty() ? "" : ", ";
			joined += name;
		}

		gfatal("{} binds '{}' members no PSO declares: {}", m_Binder, cbuffer, joined);
	}
}
