# Bloom

Wide kernel bloom from a progressive mip chain, without a threshold and with firefly suppression.

## Details

Bloom downsamples the color target through a mip chain with a thirteen tap filter and upsamples with a tent filter, which supports a very large kernel cheaply and avoids the naive threshold approach the brief rejects. Firefly reduction applies a karis average on the first downsample so a single bright pixel does not flicker between frames. Intensity, radius and the firefly clamp are fields on a Bloom component.

Verification: unit tests cover the component and the mip chain sizing at odd resolutions. Visual scenes render emissive primitives against a dark background and a single very bright pixel across several frames to show stable firefly behaviour, and a benchmark reports the pass cost at 1080p.
