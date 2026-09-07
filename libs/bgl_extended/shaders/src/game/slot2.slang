import lib.forward.GameSurface;

// Reserved game slot 2, unfilled: what the tree ships under this name. Registering a surface into
// the slot loads a module from source under the same name, which shadows this file.
public typealias Slot2Surface = NullSurface;
