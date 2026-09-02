#pragma once

namespace Rml
{
	class Context;
	class Element;
}

namespace game::test
{
	/**
	 * A context's element tree as text: one line per element, indented by depth, carrying the tag,
	 * the id and classes it has, and the computed border box after the last `Update()`.
	 *
	 *     body #menu  (0,0 800x600)
	 *       div .panel  (32,32 240x120)
	 *         button #play  (40,48 120x32)
	 *
	 * A case asserts or diffs a whole layout in one call rather than reaching for six elements by
	 * id, and a failing golden logs this beside the image -- which is what makes a layout
	 * regression readable without a debugger.
	 *
	 * Positions are absolute and in pixels, rounded to the nearest integer: the assertion is about
	 * where a box landed, and a sub-pixel difference between two platforms' text shaping is not a
	 * layout change.
	 */
	[[nodiscard]] std::string
	DumpTree(Rml::Context& context);

	/** The same, rooted at one element. */
	[[nodiscard]] std::string
	DumpTree(Rml::Element& element);
}
