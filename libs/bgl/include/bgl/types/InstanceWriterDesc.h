#pragma once
#include <string>
#include <utility>

namespace bgl
{
	/**
	 * A kernel that places a block's instances, as a Slang type conforming to
	 * `bgl.InstanceWriter`'s IInstanceWriter. The module is resolved on the GPU context's search
	 * paths, as a game surface's is.
	 */
	struct InstanceWriterDesc
	{
		std::string module;

		// The conforming type's name as `module` declares it.
		std::string type;

		template <typename Self>
		Self&&
		SetModule(this Self&& self, std::string module) noexcept
		{
			self.module = std::move(module);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetType(this Self&& self, std::string type) noexcept
		{
			self.type = std::move(type);
			return std::forward<Self>(self);
		}
	};
}
