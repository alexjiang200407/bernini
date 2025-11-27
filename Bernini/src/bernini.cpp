#include <Core/win/WinAPI.h>
#include <Core/win/Window.h>

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	auto wnd = core::win::Window{};

	while (wnd.PollEvents())
	{
		class EventVisitor : public core::win::IWindowEventVisitor
		{
			void
			Visit(const core::win::KeyEvent&) override
			{
				OutputDebugString("Key event received\n");
			}

			void
			Visit(const core::win::MouseEvent&) override
			{
				OutputDebugString("Mouse event received\n");
			}
		};

		auto visitor = EventVisitor{};
		wnd.Accept(visitor);
		wnd.Flush();
	}

	return 0;
}