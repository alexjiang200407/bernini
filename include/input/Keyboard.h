#pragma once
#include "input/KeyState.h"

class Keyboard
{
public:
	const KeyState& GetKeyState(int key) const;
	void KeyPress(int key, int repeat, bool extended, bool isFirst);
	void KeyRelease(int key);
private:
	static constexpr int NKEYS = 256;
	KeyState keyStates[NKEYS] = {};
private:
};