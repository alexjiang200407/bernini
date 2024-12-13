#pragma once
#include "input/KeyState.h"
#include "events/EventSource.h"
#include <bitset>

class KeyEvent;

class Keyboard : public EventSource<KeyEvent>
{
public:
	const KeyState& GetKeyState(int key) const;
	void KeyPress(int key, int repeat, bool extended, bool isFirst);
	void KeyRelease(int key);

public:
	static constexpr int NKEYS = 256;
	typedef std::bitset<Keyboard::NKEYS> KeyMask;
private:
	KeyState keyStates[NKEYS] = {};
	KeyMask keysDown;

};