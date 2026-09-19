#pragma once

#include <editor_api/IEditorRegistry.h>
#include <memory>
#include <string_view>

namespace editor
{
	class IEditorPlugin
	{
	public:
		virtual ~IEditorPlugin() = default;

		/** GUI-thread startup before any project; this object must outlive all its contributions. */
		virtual void
		Register(IEditorRegistry& registry) = 0;
	};

	using EditorPluginPtr = std::unique_ptr<IEditorPlugin>;

	/** Called only after build compatibility is checked; ownership transfers to the host. */
	using CreateEditorPlugin                                   = IEditorPlugin* (*)();
	inline constexpr std::string_view c_EditorPluginEntryPoint = "BerniniCreateEditorPlugin";
}
