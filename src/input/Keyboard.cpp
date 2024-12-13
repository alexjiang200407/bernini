#include "input/Keyboard.h"
#include "input/KeyEvent.h"

const KeyState& Keyboard::GetKeyState(int key) const
{
	return keyStates[key];
}

void Keyboard::KeyPress(int key, int repeat, bool extended, bool isFirst)
{
	keyStates[key].Press(repeat, extended, isFirst);
	keysDown.set(key, 1);
}

void Keyboard::KeyRelease(int key)
{
	keyStates[key].Release();
	keysDown.set(key, 0);
}
