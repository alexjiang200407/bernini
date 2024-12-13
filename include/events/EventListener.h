#pragma once

template <typename E>
class EventSource;

template <typename E>
class EventListener
{
public:
	virtual void HandleEvent(const E& evt, EventSource<E>* src) = 0;
	virtual ~EventListener() = default;
};