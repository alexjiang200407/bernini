#include "events/KeyEventListener.h"

void KeyEventListener::HandleEvent(const KeyEvent& evt, EventSource<KeyEvent>* src)
{
	if ((evt.keysDown & GetRequiredKeys()) != GetRequiredKeys()) return;

	HandleKeyEvent(evt, src);
}
