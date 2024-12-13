#pragma once
#include <chrono>

class KeyState
{
using TimePoint = std::chrono::steady_clock::time_point;
public:
	enum class Type { Release = 0, Invalid, Press };
public:
	KeyState() = default;
	KeyState(Type type);
public:
	Type GetType() const;
	int GetRepeat() const;
	bool IsFirst() const;
	bool IsExtended() const;
	
	void Press(int repeat, bool extended, bool isFirst);
	void Release();
	float GetHeldMs() const;
	
private:

	Type type = Type::Release;
	int repeat = 0;
	bool extended = false;
	bool first = false;
	TimePoint pressedStart;
	float heldMs;
};