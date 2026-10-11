// One entry in the frame's light-instance list. The scene's authored lights
// (PointLightComponent: omni or spot) become instances here each frame; the
// deferred path uploads them as a storage buffer, the forward path through
// its per-frame ring.
//
// Keep the values in sync with PointLightComponent::Type -- the extraction
// casts the component enum straight into `kind`.
enum LightInstanceKind : uint
{
    Omni = 0,
    Spot = 1,
};
