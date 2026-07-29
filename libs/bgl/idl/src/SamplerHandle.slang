// On WGSL the bindless .Handle form does not exist; a sampler is a plainly bound object the
// backend resolves into a bind-group entry. The API is identical, so call sites do not change.
#if defined(BGL_WGSL)
public typealias SamplerHandle = SamplerState;
#else
public typealias SamplerHandle = SamplerState.Handle;
#endif
