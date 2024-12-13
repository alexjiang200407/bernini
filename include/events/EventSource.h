#pragma once

template <typename E>
class EventListener;

template <typename E>
class EventSource
{
public:
	void RegisterListener(EventListener<E>* listener);
	
	void UnregisterListener(EventListener<E>* listener);

	void DispatchEvents(const E evt);
private:
	std::set<EventListener<E>*> listeners;
};

#include "EventListener.h"

template<typename E>
inline void EventSource<E>::RegisterListener(EventListener<E>* listener)
{
	listeners.insert(listener);
}

template<typename E>
inline void EventSource<E>::UnregisterListener(EventListener<E>* listener)
{
	listeners.erase(listener);
}

template<typename E>
inline void EventSource<E>::DispatchEvents(const E evt)
{
	for (auto listener : listeners)
	{
		listener->HandleEvent(evt, this);
	}
}
