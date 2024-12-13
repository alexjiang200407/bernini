#include "input/Keyboard.h"


const KeyState& Keyboard::GetKeyState(int key) const
{
	return keyStates[key];
}

void Keyboard::KeyPress(int key, int repeat, bool extended, bool isFirst)
{
	keyStates[key].Press(repeat, extended, isFirst);
}

void Keyboard::KeyRelease(int key)
{
	keyStates[key].Release();
}
