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

}

void KeyState::Release()
{
	memset(this, 0, sizeof(KeyState));
	this->type = KeyState::Type::Release;
}

float KeyState::GetHeldMs() const
{
	using namespace std::chrono;
	TimePoint now = high_resolution_clock::now();
	duration<float, std::milli> elapsed = now - pressedStart;

	return elapsed.count();
}
