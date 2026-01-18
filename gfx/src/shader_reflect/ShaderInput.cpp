#include "shader_reflect/ShaderInput.h"

namespace gfx
{
	bool
	semanticMatches(
		std::string_view elementName,
		std::string_view semanticName,
		uint32_t         semanticIndex)
	{
		if (!elementName.starts_with(semanticName))
			return false;

		auto suffix = elementName.substr(semanticName.size());

		if (suffix.empty())
			return semanticIndex == 0;

		uint32_t index = 0;
		for (char c : suffix)
		{
			if (c < '0' || c > '9')
				return false;

			index = index * 10 + (c - '0');
		}

		return index == semanticIndex;
	}
}
