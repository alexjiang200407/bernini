import lib.forward.GameSurface;

// Reserved game slot 0, unfilled: what the tree ships under this name. Registering a surface into
// the slot loads a module from source under the same name, which shadows this file.
public typealias Slot0Surface = NullSurface;
