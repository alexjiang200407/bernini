#include "ui/UiTree.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

namespace game::test
{
	namespace
	{
		void
		Append(std::string& out, Rml::Element& element, int depth)
		{
			out.append(static_cast<size_t>(depth) * 2, ' ');
			out += element.GetTagName();

			if (const std::string id = element.GetId(); !id.empty())
			{
				out += " #" + id;
			}

			// Space-separated, in the order the element carries them, so a diff is stable.
			const std::string classes = element.GetClassNames();
			for (size_t at = 0; at < classes.size();)
			{
				const size_t end  = classes.find(' ', at);
				const size_t stop = end == std::string::npos ? classes.size() : end;

				if (stop > at)
				{
					out += " ." + classes.substr(at, stop - at);
				}
				at = stop + 1;
			}

			const Rml::Vector2f offset = element.GetAbsoluteOffset(Rml::BoxArea::Border);
			const Rml::Vector2f size   = element.GetBox().GetSize(Rml::BoxArea::Border);

			out += std::format(
				"  ({},{} {}x{})\n",
				std::lround(offset.x),
				std::lround(offset.y),
				std::lround(size.x),
				std::lround(size.y));

			for (int i = 0; i < element.GetNumChildren(); ++i)
			{
				Append(out, *element.GetChild(i), depth + 1);
			}
		}
	}

	std::string
	DumpTree(Rml::Context& context)
	{
		Rml::Element* root = context.GetRootElement();
		return root != nullptr ? DumpTree(*root) : std::string();
	}

	std::string
	DumpTree(Rml::Element& element)
	{
		std::string out;
		Append(out, element, 0);
		return out;
	}
}
