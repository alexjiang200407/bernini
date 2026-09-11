#pragma once

#include <QtNodes/NodeDelegateModel>

#include <assetlib_structs/BMaterial.h>
#include <filesystem>
#include <qtmetamacros.h>

#include <QtNodes/internal/NodeDelegateModel.hpp>

/**
 * A material graph's sink: the node the material compiles from.
 *
 * MaterialGraphModel finds, guards and swaps the sink through this type, so a graph holds exactly
 * one of these whatever kind it is. A sink owns what the material says -- the shading model, that
 * model's parameters, and the layer keys -- and nothing else about the document.
 */
class MaterialSinkNode : public QtNodes::NodeDelegateModel
{
	Q_OBJECT

public:
	/**
	 * Writes what this sink authors into `material`: the shading model, its parameters and the
	 * layer keys. Texture references are stored relative to `dataRoot`, like every asset
	 * reference. The rest of the document -- the name, the serialized board, the baked state --
	 * is the caller's.
	 */
	virtual void
	CompileInto(assetlib::BMaterial& material, const std::filesystem::path& dataRoot) const = 0;

Q_SIGNALS:
	// Something the compiled material depends on changed; the window recompiles the preview on it.
	void
	Changed();
};
