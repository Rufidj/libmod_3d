/*
 * libmod_3d_water_spectrum.h - Spectral (FFT) ocean waves
 *
 * Tessendorf-style ocean: instead of adding a handful of sine waves, an entire
 * wave field is synthesised from an oceanographic energy spectrum. Each grid
 * point gets a random phase weighted by how much energy the wind puts into that
 * wavelength and direction, and an inverse FFT turns that frequency-domain field
 * into a tiling displacement map. The result has the statistics of real water --
 * no visible repetition, crests that gather and disperse -- which a sum of sines
 * cannot produce at any octave count.
 *
 * Runs entirely on the GPU in compute shaders, so it needs G3D_TIER_ULTRA (or at
 * least g3d_glcaps()->compute). Where that is unavailable the water renderer
 * falls back to its Gerstner path and never calls into here.
 *
 * CASCADES: one FFT can only cover a limited band of wavelengths before it
 * either aliases the small waves or tiles visibly on the large ones. Several
 * cascades at different patch sizes are summed, each contributing its own band.
 */

#ifndef __LIBMOD_3D_WATER_SPECTRUM_H
#define __LIBMOD_3D_WATER_SPECTRUM_H

#ifdef __cplusplus
extern "C" {
#endif

#define G3D_WATER_CASCADES 3

/* Prepare the spectrum machinery. `resolution` must be a power of two (256 is
   the sensible default; 512 costs 4x for a modest gain). Returns 0 if compute
   shaders are unavailable or anything failed to build -- callers must then use
   their fallback path. Safe to call repeatedly. */
int g3d_water_spectrum_init(int resolution);

int g3d_water_spectrum_ready(void);

/* Wind drives everything: direction (world XZ) and speed in units/second. A
   higher `fetch_scale` (0..1) means a longer wind fetch and therefore bigger,
   longer-period swell. Changing any of these regenerates the base spectrum,
   which is a one-off cost, so do not call it every frame with the same values --
   it already ignores no-op updates. */
void g3d_water_spectrum_set_wind(float dir_x, float dir_z, float speed,
                                 float fetch_scale);

/* Overall wave height multiplier and how much horizontal "choppiness" is
   applied (0 = round swell, 1 = sharp peaked crests). */
void g3d_water_spectrum_set_amplitude(float amplitude, float choppiness);

/* Advance the wave field to time `t` (seconds) and run the inverse FFTs. Call
   once per frame before rendering the water. */
void g3d_water_spectrum_update(float t);

/* Bind the cascade outputs for sampling. `unit_displacement` and
   `unit_derivative` receive array textures holding all cascades. Returns the
   number of cascades bound, or 0 if not ready. */
int g3d_water_spectrum_bind(int unit_displacement, int unit_derivative);

/* World-space size of each cascade's tile, for the shader's UV scaling. Writes
   G3D_WATER_CASCADES floats. */
void g3d_water_spectrum_tile_sizes(float *out);

void g3d_water_spectrum_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_SPECTRUM_H */
