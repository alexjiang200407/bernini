#pragma once
#include <SDL3/SDL_events.h>

namespace Rml
{
	class Context;
}

namespace demo
{
	/**
	 * Translates one SDL event into RmlUi's own input calls.
	 *
	 * The vocabulary is RmlUi's by design: bernini declares no event type of its own, so this
	 * lives beside the window that produces the events rather than in the engine (plan ADR-10).
	 * A game with a real input layer maps that layer onto the same `Context::Process*` calls.
	 *
	 * @return false when the UI consumed the event -- which is true whenever the cursor is over
	 *         an interactive element, so it answers "did this land on nothing", not "is the
	 *         cursor over a particular panel". True for every event kind this does not translate.
	 */
	bool
	ForwardToUi(Rml::Context& context, const SDL_Event& event);
}
