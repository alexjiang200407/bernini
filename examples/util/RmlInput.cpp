#include "RmlInput.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

namespace demo
{
	namespace
	{
		// RmlUi wants a button index, and SDL numbers its buttons from one.
		[[nodiscard]] int
		ButtonIndex(Uint8 sdlButton) noexcept
		{
			switch (sdlButton)
			{
			case SDL_BUTTON_LEFT:
				return 0;
			case SDL_BUTTON_RIGHT:
				return 1;
			case SDL_BUTTON_MIDDLE:
				return 2;
			default:
				return static_cast<int>(sdlButton) - 1;
			}
		}

		[[nodiscard]] int
		Modifiers(SDL_Keymod mod) noexcept
		{
			int state = 0;
			if ((mod & SDL_KMOD_SHIFT) != 0)
			{
				state |= Rml::Input::KM_SHIFT;
			}
			if ((mod & SDL_KMOD_CTRL) != 0)
			{
				state |= Rml::Input::KM_CTRL;
			}
			if ((mod & SDL_KMOD_ALT) != 0)
			{
				state |= Rml::Input::KM_ALT;
			}
			if ((mod & SDL_KMOD_GUI) != 0)
			{
				state |= Rml::Input::KM_META;
			}
			return state;
		}
	}

	bool
	ForwardToUi(Rml::Context& context, const SDL_Event& event)
	{
		const int mods = Modifiers(SDL_GetModState());

		switch (event.type)
		{
		case SDL_EVENT_MOUSE_MOTION:
			return context.ProcessMouseMove(
				static_cast<int>(event.motion.x),
				static_cast<int>(event.motion.y),
				mods);

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			return context.ProcessMouseButtonDown(ButtonIndex(event.button.button), mods);

		case SDL_EVENT_MOUSE_BUTTON_UP:
			return context.ProcessMouseButtonUp(ButtonIndex(event.button.button), mods);

		case SDL_EVENT_MOUSE_WHEEL:
			// RmlUi scrolls positive right and down; SDL reports x the same way and y inverted.
			// The float overload is deprecated and drops horizontal scroll entirely.
			return context.ProcessMouseWheel(Rml::Vector2f(event.wheel.x, -event.wheel.y), mods);

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			context.ProcessMouseLeave();
			return true;

		// Keys, text and the rest are the game's until a document wants them; a UI that takes
		// text input maps SDL_EVENT_TEXT_INPUT onto ProcessTextInput here.
		default:
			return true;
		}
	}
}
