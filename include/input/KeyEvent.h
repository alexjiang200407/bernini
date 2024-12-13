#pragma once
#include <bitset>
#include "Keyboard.h"

struct KeyEvent
{
	KeyEvent(Keyboard::KeyMask keysDown, KeyState keyStates[]);

	Keyboard::KeyMask keysDown;
	KeyState* keyStates;
};