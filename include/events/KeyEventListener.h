#pragma once
#include "events/EventListener.h"
#include "input/KeyEvent.h"

class KeyEventListener :
	public EventListener<KeyEvent>
{
public:
	virtual void HandleEvent(const KeyEvent& evt, EventSource<KeyEvent>* src) override;
	virtual void HandleKeyEvent(const KeyEvent& evt, EventSource<KeyEvent>* src) = 0;
	virtual Keyboard::KeyMask GetRequiredKeys() const = 0;
};