#include "input/KeyState.h"

KeyState::KeyState(Type type) :
	type(type)
{
}

KeyState::Type KeyState::GetType() const { return type; }

int KeyState::GetRepeat() const { return 0; }

bool KeyState::IsFirst() const { return first; }

bool KeyState::IsExtended() const { return extended; }

void KeyState::Press(int repeat, bool extended, bool isFirst)
{
	using namespace std::chrono;
	this->type = KeyState::Type::Press;
	this->extended = extended;
	this->first = isFirst;
	
	TimePoint now = high_resolution_clock::now();
	if(isFirst)
		pressedStart = now;
	duration<float, std::milli> elapsed = now - pressedStart;
	heldMs = elapsed.count();
}

void KeyState::Release()
{
	memset(this, 0, sizeof(KeyState));
	this->type = KeyState::Type::Release;
}

float KeyState::GetHeldMs() const { return heldMs; }
