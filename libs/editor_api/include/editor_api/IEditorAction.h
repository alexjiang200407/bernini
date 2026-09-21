#pragma once
#include <concepts>
#include <editor_api/IEditorHost.h>
#include <memory>
#include <span>
#include <string>

namespace editor
{
	// Registry-owned; call arguments are borrowed and must not be retained.
	class IEditorAction
	{
	public:
		virtual ~IEditorAction()            = default;
		IEditorAction(const IEditorAction&) = delete;
		IEditorAction(IEditorAction&&)      = delete;
		IEditorAction&
		operator=(const IEditorAction&) = delete;
		IEditorAction&
		operator=(IEditorAction&&) = delete;

		virtual bool
		IsEnabled(IEditorHost& host, std::span<const std::string> selectedAssets) const = 0;

		virtual void
		Invoke(IEditorHost& host, std::span<const std::string> selectedAssets) = 0;

	protected:
		IEditorAction() = default;
	};
	using EditorActionPtr = std::unique_ptr<IEditorAction>;

	template <typename T, typename... Args>
	concept EditorActionFor =
		std::derived_from<T, IEditorAction> && std::constructible_from<T, Args...>;

}
