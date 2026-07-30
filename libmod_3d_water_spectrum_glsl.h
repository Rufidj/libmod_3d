/*
 * libmod_3d_water_spectrum_glsl.h - Compute shaders for the spectral ocean
 *
 * The pipeline, once per cascade per frame:
 *
 *   h0        (once)  Phillips spectrum x Gaussian noise -> the sea's "identity"
 *   spectrum  (frame) advance h0 to time t, and build the three complex fields
 *                     to transform: height, and the two horizontal displacements
 *   fft       (frame) 2*log2(N) butterfly passes: rows, then columns
 *   assemble  (frame) unshift, normalise, and write displacement + derivatives
 *
 * All stages are prefixed with `#version 430\n` (or 310 es) by the C side.
 */

#ifndef __LIBMOD_3D_WATER_SPECTRUM_GLSL_H
#define __LIBMOD_3D_WATER_SPECTRUM_GLSL_H

/* ---------------------------------------------------------------------------
   Butterfly lookup table: for every FFT stage and index, the twiddle factor and
   the two input indices. Precomputed once so the transform passes stay branchless.
   Texture is log2(N) wide by N tall.
   --------------------------------------------------------------------------- */
static const char *g3d_wspec_glsl_butterfly =
    "layout(local_size_x = 1, local_size_y = 16) in;\n"
    "layout(rgba32f, binding = 0) writeonly uniform image2D uButterfly;\n"
    "uniform int uN;\n"
    "uniform int uStages;\n"
    "const float TAU = 6.283185307179586;\n"
    "\n"
    "int bitReverse(int x, int bits) {\n"
    "    int r = 0;\n"
    "    for (int i = 0; i < bits; i++) { r = (r << 1) | (x & 1); x >>= 1; }\n"
    "    return r;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"   /* x = stage, y = index */
    "    if (id.x >= uStages || id.y >= uN) return;\n"
    "    int span = 1 << id.x;\n"
    /* Twiddle exp(+i*TAU*k/N): the POSITIVE sign makes this an inverse
       transform, which is what turns a spectrum back into a height field. */
    "    float k = mod(float(id.y) * float(uN) / float(span << 1), float(uN));\n"
    "    vec2 tw = vec2(cos(TAU * k / float(uN)), sin(TAU * k / float(uN)));\n"
    "    bool topWing = (id.y & span) == 0;\n"
    "\n"
    "    if (id.x == 0) {\n"
    /*     First stage also performs the bit-reversal permutation, so no separate
           reorder pass is needed. */
    "        int a = topWing ? id.y : id.y - 1;\n"
    "        imageStore(uButterfly, id, vec4(tw, float(bitReverse(a, uStages)),\n"
    "                                            float(bitReverse(a + 1, uStages))));\n"
    "    } else {\n"
    "        int a = topWing ? id.y : id.y - span;\n"
    "        imageStore(uButterfly, id, vec4(tw, float(a), float(a + span)));\n"
    "    }\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Base spectrum h0(k). Depends only on wind and cascade, so it is regenerated
   only when those change, never per frame.
   --------------------------------------------------------------------------- */
static const char *g3d_wspec_glsl_h0 =
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(rgba32f, binding = 0) writeonly uniform image2D uH0;\n"
    "uniform int   uN;\n"
    "uniform float uTileSize;\n"     /* world units covered by this cascade   */
    "uniform vec2  uWindDir;\n"
    "uniform float uWindSpeed;\n"
    "uniform float uAmplitude;\n"
    "uniform float uFetch;\n"
    "uniform vec2  uBandLimit;\n"    /* keep only |k| within (min,max)        */
    "uniform int   uSeed;\n"
    "const float G = 9.81;\n"
    "const float TAU = 6.283185307179586;\n"
    "\n"
    "float hash(vec2 p, float s) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031 + s * 0.0007);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "\n"
    /* Box-Muller: two uniform randoms -> two independent normals. The wave field
       is a Gaussian random process; using uniform noise directly would give the
       wrong crest statistics. */
    "vec2 gaussian(vec2 p) {\n"
    "    float u1 = max(hash(p, float(uSeed)), 1.0e-6);\n"
    "    float u2 = hash(p, float(uSeed) + 137.0);\n"
    "    float r = sqrt(-2.0 * log(u1));\n"
    "    return vec2(r * cos(TAU * u2), r * sin(TAU * u2));\n"
    "}\n"
    "\n"
    /* Phillips spectrum: energy the wind has put into wavevector k. */
    "float phillips(vec2 k) {\n"
    "    float kk = dot(k, k);\n"
    "    if (kk < 1.0e-12) return 0.0;\n"
    "    float kLen = sqrt(kk);\n"
    "    float L = uWindSpeed * uWindSpeed / G;\n"          /* largest wind wave */
    "    L *= max(uFetch, 0.05);\n"
    "    float kL = kLen * L;\n"
    "    float dirTerm = dot(normalize(k), uWindDir);\n"
    /* Waves travelling against the wind are suppressed, not mirrored: a squared
       cosine alone would make the sea symmetric and it would not look wind-blown. */
    "    float dir2 = dirTerm * dirTerm;\n"
    "    if (dirTerm < 0.0) dir2 *= 0.08;\n"
    "    float damp = exp(-kk * (L * 0.0015) * (L * 0.0015));\n"  /* kill ripples below the grid */
    "    return uAmplitude * exp(-1.0 / max(kL * kL, 1.0e-6)) / (kk * kk) * dir2 * damp;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"
    "    if (id.x >= uN || id.y >= uN) return;\n"
    "    float n = float(uN);\n"
    /* Centre the wavevector grid on zero: indices run -N/2 .. N/2-1. */
    "    vec2 k = vec2(float(id.x) - n * 0.5, float(id.y) - n * 0.5) * (TAU / uTileSize);\n"
    "    float kLen = length(k);\n"
    "\n"
    /* Band-limit each cascade to the wavelengths it owns, otherwise neighbouring
       cascades both carry the same waves and the sum doubles their height. */
    "    if (kLen < uBandLimit.x || kLen >= uBandLimit.y) {\n"
    "        imageStore(uH0, id, vec4(0.0));\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec2 g1 = gaussian(vec2(id));\n"
    "    vec2 g2 = gaussian(vec2(id) + vec2(7.13, 3.71));\n"
    "    float h  = sqrt(max(phillips(k), 0.0) * 0.5);\n"
    "    float hc = sqrt(max(phillips(-k), 0.0) * 0.5);\n"
    /* .xy = h0(k), .zw = conjugate of h0(-k), which the time pass needs to keep
       the height field real-valued. */
    "    imageStore(uH0, id, vec4(g1 * h, g2.x * hc, -g2.y * hc));\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Advance to time t and build the fields to transform.
   Output A = (height, displacementX), Output B = (displacementZ, unused).
   --------------------------------------------------------------------------- */
static const char *g3d_wspec_glsl_spectrum =
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(rgba32f, binding = 0) readonly  uniform image2D uH0;\n"
    "layout(rgba32f, binding = 1) writeonly uniform image2D uOutA;\n"
    "layout(rgba32f, binding = 2) writeonly uniform image2D uOutB;\n"
    "uniform int   uN;\n"
    "uniform float uTileSize;\n"
    "uniform float uTime;\n"
    "const float G = 9.81;\n"
    "const float TAU = 6.283185307179586;\n"
    "\n"
    "vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }\n"
    "\n"
    "void main() {\n"
    "    ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"
    "    if (id.x >= uN || id.y >= uN) return;\n"
    "    float n = float(uN);\n"
    "    vec2 k = vec2(float(id.x) - n * 0.5, float(id.y) - n * 0.5) * (TAU / uTileSize);\n"
    "    float kLen = length(k);\n"
    "\n"
    "    vec4 h0 = imageLoad(uH0, id);\n"
    /* Deep-water dispersion: long waves travel faster. This single relation is
       what makes the sea evolve like water rather than scrolling noise. */
    "    float w = sqrt(G * max(kLen, 1.0e-6));\n"
    "    float c = cos(w * uTime), s = sin(w * uTime);\n"
    "    vec2 e  = vec2(c, s);\n"
    "    vec2 ec = vec2(c, -s);\n"
    "    vec2 h = cmul(h0.xy, e) + cmul(h0.zw, ec);\n"
    "\n"
    /* Horizontal displacement is -i * (k/|k|) * h; multiplying a complex value
       by -i is a 90 degree rotation, hence the swizzle and sign. */
    "    vec2 kn = (kLen > 1.0e-6) ? k / kLen : vec2(0.0);\n"
    "    vec2 ih = vec2(h.y, -h.x);\n"
    "    vec2 dx = ih * kn.x;\n"
    "    vec2 dz = ih * kn.y;\n"
    "\n"
    "    imageStore(uOutA, id, vec4(h, dx));\n"
    "    imageStore(uOutB, id, vec4(dz, 0.0, 0.0));\n"
    "}\n";

/* ---------------------------------------------------------------------------
   One butterfly pass. Transforms BOTH complex values packed in the RGBA texture
   at once (xy and zw are independent fields sharing the same twiddles).
   --------------------------------------------------------------------------- */
static const char *g3d_wspec_glsl_fft =
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(rgba32f, binding = 0) readonly  uniform image2D uSrc;\n"
    "layout(rgba32f, binding = 1) writeonly uniform image2D uDst;\n"
    "layout(rgba32f, binding = 2) readonly  uniform image2D uButterfly;\n"
    "uniform int uStage;\n"
    "uniform int uVertical;\n"
    "uniform int uN;\n"
    "\n"
    "vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }\n"
    "\n"
    "void main() {\n"
    "    ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"
    "    if (id.x >= uN || id.y >= uN) return;\n"
    /* Along rows first, then along columns: a 2D FFT is just 1D transforms in
       each direction. */
    "    int line = (uVertical == 0) ? id.x : id.y;\n"
    "    vec4 bf = imageLoad(uButterfly, ivec2(uStage, line));\n"
    "    int ia = int(bf.z), ib = int(bf.w);\n"
    "    ivec2 pa = (uVertical == 0) ? ivec2(ia, id.y) : ivec2(id.x, ia);\n"
    "    ivec2 pb = (uVertical == 0) ? ivec2(ib, id.y) : ivec2(id.x, ib);\n"
    "\n"
    "    vec4 a = imageLoad(uSrc, pa);\n"
    "    vec4 b = imageLoad(uSrc, pb);\n"
    "    vec2 tw = bf.xy;\n"
    "    vec4 r = vec4(a.xy + cmul(tw, b.xy), a.zw + cmul(tw, b.zw));\n"
    "    imageStore(uDst, id, r);\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Final pass: undo the centring, normalise, and pack the cascade's outputs.
   --------------------------------------------------------------------------- */
static const char *g3d_wspec_glsl_assemble =
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(rgba32f, binding = 0) readonly  uniform image2D uA;\n"
    "layout(rgba32f, binding = 1) readonly  uniform image2D uB;\n"
    "layout(rgba16f, binding = 2) writeonly uniform image2DArray uDisplacement;\n"
    "layout(rgba16f, binding = 3) writeonly uniform image2DArray uDerivative;\n"
    "uniform int   uN;\n"
    "uniform int   uLayer;\n"
    "uniform float uTileSize;\n"
    "uniform float uChoppy;\n"
    "\n"
    /* The spectrum was built centred on k=0, which in the spatial domain shows up
       as a checkerboard sign flip. Undo it here. */
    "float unshift(ivec2 id) { return (((id.x + id.y) & 1) == 0) ? 1.0 : -1.0; }\n"
    "\n"
    "vec3 sampleDisp(ivec2 id) {\n"
    "    ivec2 p = ivec2(id.x & (uN - 1), id.y & (uN - 1));\n"
    "    float s = unshift(p);\n"
    "    float hy = imageLoad(uA, p).x * s;\n"
    "    float hx = imageLoad(uA, p).z * s;\n"
    "    float hz = imageLoad(uB, p).x * s;\n"
    "    return vec3(hx * uChoppy, hy, hz * uChoppy);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"
    "    if (id.x >= uN || id.y >= uN) return;\n"
    "\n"
    "    vec3 d = sampleDisp(id);\n"
    "    vec3 dxp = sampleDisp(id + ivec2(1, 0));\n"
    "    vec3 dxm = sampleDisp(id - ivec2(1, 0));\n"
    "    vec3 dzp = sampleDisp(id + ivec2(0, 1));\n"
    "    vec3 dzm = sampleDisp(id - ivec2(0, 1));\n"
    "    float step = uTileSize / float(uN);\n"
    "    float inv2 = 1.0 / (2.0 * step);\n"
    "\n"
    "    vec2 slope = vec2((dxp.y - dxm.y) * inv2, (dzp.y - dzm.y) * inv2);\n"
    "\n"
    /* Jacobian of the horizontal displacement. Below zero the surface has folded
       over itself -- that is physically where a wave is breaking, and it is the
       most convincing place to put foam, because it tracks the actual geometry
       instead of a height threshold. */
    "    float jxx = 1.0 + (dxp.x - dxm.x) * inv2;\n"
    "    float jzz = 1.0 + (dzp.z - dzm.z) * inv2;\n"
    "    float jxz = (dzp.x - dzm.x) * inv2;\n"
    "    float jzx = (dxp.z - dxm.z) * inv2;\n"
    "    float jacobian = jxx * jzz - jxz * jzx;\n"
    "\n"
    "    imageStore(uDisplacement, ivec3(id, uLayer), vec4(d, 0.0));\n"
    "    imageStore(uDerivative, ivec3(id, uLayer), vec4(slope, jacobian, 0.0));\n"
    "}\n";

#endif /* __LIBMOD_3D_WATER_SPECTRUM_GLSL_H */
