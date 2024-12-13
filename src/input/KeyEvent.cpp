#include "input/KeyEvent.h"

KeyEvent::KeyEvent(Keyboard::KeyMask keysDown, KeyState keyStates[]) :
	keysDown(keysDown), keyStates(keyStates)
{}
