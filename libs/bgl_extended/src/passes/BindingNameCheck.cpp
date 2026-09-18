#include "passes/BindingNameCheck.h"
#include "pipeline/MeshletKernel.h"
#include "uniforms/Uniforms.h"
#include <algorithm>
#include <bgl_common/UniformsBase.h>
#include <bgl_common/gassert.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bgl
{
	BindingNameCheck&
	BindingNameCheck::Check(std::string_view cbuffer, std::span<const std::string_view> names)
	{
		std::vector<const Uniforms*> variants;
		variants.reserve(m_Kernels.size());

		for (const MeshletKernel& kernel : m_Kernels)
		{
			// A demand-built family holds unbuilt kernels; one is not a variant, it is an absence.
			if (!kernel.pipeline.IsInitialized())
			{
				continue;
			}

			const auto found = kernel.uniforms.find(cbuffer);
			variants.push_back(found != kernel.uniforms.end() ? &found->second : nullptr);
		}

		// Under demand building the built subset may hold no variant with this cbuffer at all --
		// every skinned bucket unbuilt leaves 'skinnedData' nowhere -- and that is absence, not a
		// typo. The member check resumes with the first build that carries the cbuffer.
		if (std::ranges::none_of(variants, [](const Uniforms* uniforms) {
				return uniforms != nullptr;
			}))
		{
			return *this;
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
