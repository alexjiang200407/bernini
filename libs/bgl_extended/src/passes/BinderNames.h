#pragma once
#include "pipeline/MeshletKernel.h"
#include "uniforms/Uniforms.h"
#include <concepts>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace bgl
{
	/** A range of bindings, each naming the constant-buffer member it binds. */
	template <typename T>
	concept UniformKeyed =
		std::ranges::input_range<T> && requires(std::ranges::range_reference_t<T> binding) {
			{ binding.uniformKey } -> std::convertible_to<std::string_view>;
		};

	/** The `uniformKey` of every binding in `bindings`, in order. */
	template <UniformKeyed T>
	std::vector<std::string_view>
	GetUniformKeys(const T& bindings)
	{
		std::vector<std::string_view> keys;
		keys.reserve(std::ranges::size(bindings));

		for (const auto& binding : bindings)
		{
			keys.push_back(binding.uniformKey);
		}

		return keys;
	}

	/**
	 * One pass's binder, checked against the built kernels of the PSO family it binds into.
	 *
	 * A variant omitting a member is ordinary and stays silent; a name *no* variant declares is a
	 * typo or a shader rename, which binding cannot report because `IsValid()` reads the same either
	 * way. The family is demand-built, so an unbuilt kernel is not a variant, and a cbuffer no built
	 * variant carries is checked only once one is -- re-run the check after every build.
	 * @pre construct one in the pass's `CheckBindings`, once a batch is built, and check every
	 * cbuffer it writes there, never per draw.
	 */
	class BinderNames final
	{
	public:
		/** @pre `kernels` outlives every `Check`; it is not copied. */
		BinderNames(std::string_view binder, std::span<const MeshletKernel> kernels) noexcept :
			m_Binder(binder), m_Kernels(kernels)
		{}

		/** @post Fatal when a name in `names` resolves in no variant's `cbuffer`. */
		BinderNames&
		Check(std::string_view cbuffer, std::span<const std::string_view> names);

	private:
		std::string_view               m_Binder;
		std::span<const MeshletKernel> m_Kernels;
	};
}
