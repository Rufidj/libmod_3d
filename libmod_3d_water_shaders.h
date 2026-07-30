/*
 * libmod_3d_water_shaders.h - GLSL for the water surface, one source per stage
 *
 * There is ONE shader body per stage, shared by every quality tier. The tier is
 * expressed as #defines in a preamble built at runtime (see water_shader_build
 * in libmod_3d_water_render.c), so the ultra and the phone paths cannot drift
 * apart the way four hand-maintained copies would.
 *
 * Preamble symbols the bodies rely on:
 *   G3D_TIER        0..3 (low/mid/high/ultra)
 *   WATER_TESS      1 when compiled into a tessellation pipeline
 *   WATER_IBL       1 when a prefiltered environment cubemap is bound
 *   WATER_SSR       1 to ray-march screen-space reflections
 *   WATER_SPECTRUM  1 when FFT displacement/normal/foam maps are bound
 *
 * Vertex layout of the grid: position.xz are world coordinates on the water
 * plane; position.y is unused. Everything else (surface level, depth, flow) is
 * read from uFieldTex, so the grid never has to be rebuilt when the water moves.
 */

#ifndef __LIBMOD_3D_WATER_SHADERS_H
#define __LIBMOD_3D_WATER_SHADERS_H

/* ---------------------------------------------------------------------------
   Shared helpers: field sampling, wave synthesis, BRDF.
   Injected into every stage that needs them.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_common =
    "uniform sampler2D uFieldTex;\n"   /* rgba = level, depth, flowX, flowZ */
    /* .xy = world xz of the field's lower corner, .zw = its extent. One vec4
       because the uniform helpers do not carry a vec2 setter. */
    "uniform vec4  uFieldOriginSize;\n"
    "uniform vec3  uCameraPos;\n"    /* every stage needs it: LOD, culling, view */
    "uniform float uSeaLevel;\n"       /* level outside the field; -1e30 = none */
    "uniform float uTime;\n"
    "uniform float uWaveAmp;\n"
    "uniform float uWaveLen;\n"
    "uniform float uWaveSpeed;\n"
    "uniform float uChoppy;\n"
    "\n"
    /* ---- Beach surf ---------------------------------------------------
       Real shore waves are not just chop: they SHOAL (grow and slow as the
       bottom rises), turn to face the beach, break into a white line, then run
       up the sand and drain back. */
    "uniform float uSurfAmount;\n"   /* 0 = off                              */
    "uniform float uSurfFreq;\n"     /* bands per unit of depth              */
    "uniform float uSurfSpeed;\n"
    "uniform float uSurfRunup;\n"    /* how far up the sand the swash reaches */
    "\n"
    /* Phase of the incoming swell at a point, driven by DEPTH rather than by
       distance. That one choice does most of the work: lines of equal phase are
       lines of equal depth, which near any shore run parallel to it, so the
       crests bend to face the beach on their own -- refraction, without tracing
       a single ray or storing a distance field. It also makes the waves crowd
       together as the bottom rises, exactly as shoaling does. */
    "uniform float uSurfHeight;\n"   /* crest height of the shore swell      */
    "uniform vec2  uSurfDir;\n"      /* where the swell comes from (unit xz) */
    "\n"
    /* Phase of the shore swell at a world position.
       Two phases blended by depth. Out at sea the swell travels the way the wind
       sends it; as it feels the bottom it turns to follow the depth contours,
       which near any shore means turning to face the beach. That blend IS
       refraction -- it is why waves arrive roughly parallel to the sand however
       they set off, and it costs one mix instead of a wave-tracing pass. */
    "float beachPhaseAt(vec2 p, float depth, float t) {\n"
    /* NEGATIVE depth term. A crest sits where the phase is constant, so with
       +depth*k the crest would have to move to ever DEEPER water as time runs:
       the surf would roll out to sea. Shoreward motion needs the depth term to
       fall as time rises. */
    "    float depPhase = -depth * uSurfFreq;\n"
    /* Out at sea the swell has a world-space wavelength, not a depth-space one.
       Roughly ten depth-bands' worth is a believable swell for a beach and keeps
       both parts driven by the single spacing control. */
    "    float dirPhase = dot(p, uSurfDir) * (uSurfFreq * 0.1);\n"
    /* The handover has to happen across the depths the player can SEE. Blending
       from 1 to 9 units put the whole surf zone (under ~4) on the depth term
       alone, so turning the direction dial changed nothing anywhere it mattered.
       From half a unit to five, the outer surf still shows the angle the swell
       arrives at and it straightens as it refracts inshore -- which is exactly
       what a real beach looks like. */
    "    float deepness = smoothstep(0.5, 5.0, depth);\n"
    "    return mix(depPhase, dirPhase, deepness) - t * uSurfSpeed;\n"
    "}\n"
    "\n"
    /* Shape of a shoaling wave. A shore wave is NOT a sine: as the bottom rises
       the crest peaks up and the troughs flatten out (a cnoidal profile), and
       that sharp crest over a long flat trough is most of what makes surf read
       as surf rather than as ripples. */
    "float surfProfile(float ph) {\n"
    "    float s = sin(ph * 6.2831853);\n"
    "    return (s > 0.0) ? pow(s, 0.55) : -0.35 * pow(-s, 1.7);\n"
    "}\n"
    "\n"
    /* Crest height at a point. Green's law: as depth falls the same energy is
       packed into a shorter, taller wave, roughly as depth^(-1/4). Capped at the
       breaker limit, because past that the wave topples instead of growing. */
    /* How close the wave here is to toppling: crest height against the breaker
       limit of 0.78*depth. 1 means it is breaking right now. */
    "float surfBreakiness(float depth) {\n"
    "    if (uSurfAmount <= 0.0 || uSurfHeight <= 0.0 || depth <= 0.0) return 0.0;\n"
    "    float amp = uSurfHeight * uSurfAmount / pow(max(depth, 0.45), 0.25);\n"
    "    return clamp(amp / max(0.78 * depth, 0.05), 0.0, 1.5);\n"
    "}\n"
    "\n"
    "float surfHeightAt(vec2 p, float depth, float t) {\n"
    "    if (uSurfAmount <= 0.0 || uSurfHeight <= 0.0 || depth <= 0.0) return 0.0;\n"
    "    float amp = uSurfHeight * uSurfAmount / pow(max(depth, 0.45), 0.25);\n"
    "    amp = min(amp, 0.78 * depth);\n"
    "    return surfProfile(beachPhaseAt(p, depth, t)) * amp;\n"
    "}\n"
    "\n"
    "vec2 waterFieldUV(vec2 worldXZ) {\n"
    "    return (worldXZ - uFieldOriginSize.xy) / uFieldOriginSize.zw;\n"
    "}\n"
    "\n"
    /* Surface level, depth and flow at a world position. Outside the field the
       sea (if any) continues to the horizon with infinite depth, which is what
       makes an ocean and a lake the same code path. */
    "vec4 waterSampleField(vec2 worldXZ) {\n"
    "    vec2 uv = waterFieldUV(worldXZ);\n"
    "    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {\n"
    "        if (uSeaLevel < -1.0e29) return vec4(0.0, -1.0, 0.0, 0.0);\n"
    "        return vec4(uSeaLevel, 100.0, 0.0, 0.0);\n"
    "    }\n"
    "    return texture(uFieldTex, uv);\n"
    "}\n"
    "\n"
    /* Sum of Gerstner waves. Returns the displacement in .xyz; the caller adds
       it to the flat surface point. `scale` fades the whole thing out in shallow
       water so waves do not punch through the shore. Tier 0/1 use this as the
       only wave source; tier 2+ layers the FFT cascades on top. */
    "vec3 waterGerstner(vec2 p, float scale, out vec3 nrm, out float crest) {\n"
    "    vec3 disp = vec3(0.0);\n"
    "    nrm = vec3(0.0, 1.0, 0.0);\n"
    "    crest = 0.0;\n"
    "    if (scale <= 0.001 || uWaveAmp <= 0.0) return disp;\n"
    "    vec2 dirs[4] = vec2[](normalize(vec2( 1.00,  0.35)),\n"
    "                          normalize(vec2(-0.45,  1.00)),\n"
    "                          normalize(vec2( 0.70, -0.60)),\n"
    "                          normalize(vec2(-0.80, -0.30)));\n"
    "    float amp = uWaveAmp * scale;\n"
    "    float len = uWaveLen;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float k = 6.2831853 / max(len, 0.05);\n"
    /*     Steepness is bounded so the wave can never fold back on itself and
           turn inside out, which is what a raw Gerstner Q does at high amplitude. */
    "        float Q = uChoppy * 0.55 / (k * amp * 4.0 + 0.001);\n"
    "        float ph = dot(dirs[i], p) * k + uTime * uWaveSpeed * (1.0 + float(i) * 0.3);\n"
    "        float c = cos(ph), s = sin(ph);\n"
    "        disp.x += Q * amp * dirs[i].x * c;\n"
    "        disp.z += Q * amp * dirs[i].y * c;\n"
    "        disp.y += amp * s;\n"
    "        nrm.x  -= dirs[i].x * (k * amp * c);\n"
    "        nrm.z  -= dirs[i].y * (k * amp * c);\n"
    "        nrm.y  -= Q * (k * amp * s);\n"
    "        crest  += s * amp;\n"
    "        amp *= 0.55;\n"
    "        len *= 0.62;\n"
    "    }\n"
    "    nrm = normalize(nrm);\n"
    "    crest = clamp(crest / (uWaveAmp * scale * 1.8 + 0.001), -1.0, 1.0);\n"
    "    return disp;\n"
    "}\n"
    "\n"
    "#if WATER_SPECTRUM\n"
    "uniform sampler2DArray uDisplacement;\n"
    "uniform sampler2DArray uDerivative;\n"
    "uniform vec4 uTileSizes;\n"      /* world size of each cascade's tile */
    "uniform float uSpectrumScale;\n"
    "\n"
    /* Sum the FFT cascades. Each one tiles at its own size, so together they
       never repeat at any scale the eye can catch -- which is the entire reason
       for using several instead of one big transform. `scale` fades the whole
       field out in shallow water, exactly like the Gerstner path.
       Returns displacement; writes the surface slope and the folding amount
       (1 - Jacobian), which is where a real wave is breaking. */
    "vec3 waterSpectrum(vec2 p, float scale, float lod, out vec2 slope, out float fold) {\n"
    "    vec3 disp = vec3(0.0);\n"
    "    slope = vec2(0.0);\n"
    "    fold = 0.0;\n"
    "    if (scale <= 0.001) return disp;\n"
    "    for (int c = 0; c < 3; c++) {\n"
    "        float tile = (c == 0) ? uTileSizes.x : ((c == 1) ? uTileSizes.y : uTileSizes.z);\n"
    "        if (tile <= 0.0) continue;\n"
    "        vec2 uv = p / tile;\n"
    /*     Vertex and tessellation stages have no screen-space derivatives, so the
           mip has to be chosen explicitly. Smaller cascades need a coarser level
           at the same distance, because they tile far more often per pixel. */
    "        float cl = max(lod - log2(max(tile, 1.0)) + 6.0, 0.0);\n"
    "        disp  += textureLod(uDisplacement, vec3(uv, float(c)), cl).xyz;\n"
    "        vec3 dv = textureLod(uDerivative, vec3(uv, float(c)), cl).xyz;\n"
    "        slope += dv.xy;\n"
    "        fold  += max(0.0, 1.0 - dv.z);\n"
    "    }\n"
    "    disp *= scale * uSpectrumScale;\n"
    "    slope *= scale * uSpectrumScale;\n"
    "    fold *= scale;\n"
    "    return disp;\n"
    "}\n"
    "\n"
    /* Fragment-side slope, sampled with the AUTOMATIC mip level.
     *
     * This is what removes the shimmer on distant water. Interpolating a normal
     * computed per vertex cannot help: by then each pixel already covers many
     * waves, and whichever one the vertex happened to land on flickers as the
     * camera moves. Letting the hardware pick the mip from screen-space
     * derivatives averages exactly the waves the pixel really covers. */
    "vec2 waterSpectrumSlopeAuto(vec2 p, float scale) {\n"
    "    vec2 slope = vec2(0.0);\n"
    "    if (scale <= 0.001) return slope;\n"
    "    for (int c = 0; c < 3; c++) {\n"
    "        float tile = (c == 0) ? uTileSizes.x : ((c == 1) ? uTileSizes.y : uTileSizes.z);\n"
    "        if (tile <= 0.0) continue;\n"
    "        slope += texture(uDerivative, vec3(p / tile, float(c))).xy;\n"
    "    }\n"
    "    return slope * scale * uSpectrumScale;\n"
    "}\n"
    "#endif\n"
    "\n"
    /* The one entry point the geometry stages call. Which wave model backs it is
       a tier decision made once, here, so the vertex and tessellation paths stay
       identical to each other and neither has to know about cascades. */
    /* Adds the shore swell on top of whatever the open-sea model produced. The
       normal comes from real finite differences of the crest height, because the
       phase depends on the DEPTH -- which varies with position in a way no
       analytic derivative here can know without reading the field anyway. */
    "vec3 waterSurfAdd(vec2 p, float t, inout vec3 nrm) {\n"
    "    if (uSurfAmount <= 0.0 || uSurfHeight <= 0.0) return vec3(0.0);\n"
    "    float d0 = waterSampleField(p).y;\n"
    "    if (d0 <= 0.0) return vec3(0.0);\n"
    "    const float e = 0.9;\n"
    "    float h0 = surfHeightAt(p, d0, t);\n"
    "    float hx = surfHeightAt(p + vec2(e, 0.0), waterSampleField(p + vec2(e, 0.0)).y, t);\n"
    "    float hz = surfHeightAt(p + vec2(0.0, e), waterSampleField(p + vec2(0.0, e)).y, t);\n"
    "    nrm = normalize(nrm + vec3(-(hx - h0) / e, 0.0, -(hz - h0) / e));\n"
    /* A shoaling crest pitches FORWARD, toward the beach -- the face steepens
       while the back stays gentle. Leaning the crest along the travel direction
       is what sells that, and it is the same trick Gerstner uses. */
    /* ...and as it nears breaking it CURLS. The throw grows sharply with how
       close the crest is to the breaker limit, so the lip runs ahead of the
       water beneath it and overhangs the trough. That is a real fold in the
       surface -- the same thing the Jacobian detects for foam -- and it is what
       separates a plunging breaker from a wave that merely leans. */
    /* ...and near breaking the LIP PITCHES OVER: forward AND down, so it ends up
     * hanging past the trough instead of sitting on top of it.
     *
     * A height field cannot hold a tube -- inside a barrel one ground position
     * has two water heights, and this stores one. What it CAN do is fold: the
     * displacement is horizontal as well as vertical, so vertices overtake one
     * another and the sheet laps over itself. Throwing the very top of the crest
     * along an arc (forward, then down) is what turns a leaning wave into an
     * overhanging one. It reads as a barrel from the beach and from inside; it
     * is not the photograph, because the spray and the thin translucent lip in
     * one of those need particles, not geometry.
     *
     * Only the top of the crest is thrown -- the water below it stays put, which
     * is precisely what makes the lip run ahead and curl. */
    "    float br = smoothstep(0.45, 1.0, surfBreakiness(d0));\n"
    "    float ampHere = uSurfHeight * uSurfAmount / pow(max(d0, 0.45), 0.25);\n"
    "    float lip = smoothstep(0.35, 1.0, h0 / max(ampHere, 0.001));\n"
    "    float thrown = br * lip * ampHere * 1.9;\n"
    "    vec2 lean = uSurfDir * (max(h0, 0.0) * 0.35 + thrown);\n"
    /* The arc: as it is thrown forward the lip also falls, which is what makes
       it overhang rather than merely stretch. */
    "    float drop = thrown * 0.85;\n"
    "    return vec3(-lean.x, h0 - drop, -lean.y);\n"
    "}\n"
    "\n"
    "vec3 waterSurface(vec2 p, float scale, out vec3 nrm, out float crest,\n"
    "                  out float fold) {\n"
    "#if WATER_SPECTRUM\n"
    "    vec2 slope;\n"
    /* Distance-based mip for the geometry. Displacing far-away vertices from
       level 0 makes the silhouette crawl as much as the shading does. */
    "    float dist = distance(uCameraPos.xz, p);\n"
    "    float lod = log2(max(dist, 1.0)) - 2.0;\n"
    /* Flatten the waves out toward the horizon. Mip-mapping fixes the SHADING,
       but near-grazing angles are a geometry problem: each pixel there spans a
       huge stretch of surface, and displaced vertices tear the silhouette into
       vertical streaks that no amount of filtering can smooth. A real sea reads
       as flat at the horizon anyway, so fading the displacement out is both the
       cheap fix and the correct look. */
    "    float far = 1.0 - smoothstep(180.0, 600.0, dist);\n"
    "    vec3 disp = waterSpectrum(p, scale * far, max(lod, 0.0), slope, fold);\n"
    "    nrm = normalize(vec3(-slope.x, 1.0, -slope.y));\n"
    "    disp += waterSurfAdd(p, uTime, nrm);\n"
    "    crest = clamp(disp.y * 1.6, -1.0, 1.0);\n"
    "    return disp;\n"
    "#else\n"
    "    fold = 0.0;\n"
    "    vec3 g = waterGerstner(p, scale, nrm, crest);\n"
    "    g += waterSurfAdd(p, uTime, nrm);\n"
    "    return g;\n"
    "#endif\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Vertex stage.
   With tessellation it is a pass-through: displacing before the patch is
   subdivided would only move the corners. Without it, this is where the surface
   is built.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_vert =
    "layout(location = 0) in vec3 position;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "\n"
    "#if WATER_TESS\n"
    "out vec2 tcWorldXZ;\n"
    "void main() {\n"
    "    tcWorldXZ = position.xz;\n"
    "}\n"
    "#else\n"
    "out vec3  vWorldPos;\n"
    "out vec3  vNormal;\n"
    "out vec4  vClip;\n"
    "out float vDepth;\n"
    "out vec2  vFlow;\n"
    "out float vCrest;\n"
    "out float vFold;\n"
    "void main() {\n"
    "    vec4 f = waterSampleField(position.xz);\n"
    "    vDepth = f.y;\n"
    "    vFlow  = f.zw;\n"
    /* Waves fade out as the water gets shallow, so the surface meets the shore
       flat instead of stabbing through the beach. */
    "    float shoal = clamp(f.y / 1.5, 0.0, 1.0);\n"
    "    vec3 nrm; float crest;\n"
    "    vec3 disp = waterSurface(position.xz, shoal, nrm, crest, vFold);\n"
    "    vec3 wp = vec3(position.x, f.x, position.z) + disp;\n"
    "    vNormal = nrm;\n"
    "    vCrest = crest;\n"
    "    vWorldPos = wp;\n"
    "    gl_Position = uProjection * uView * vec4(wp, 1.0);\n"
    "    vClip = gl_Position;\n"
    "}\n"
    "#endif\n";

/* ---------------------------------------------------------------------------
   Tessellation control: pick a subdivision level per patch, and cull.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_tcs =
    "layout(vertices = 4) out;\n"
    "in  vec2 tcWorldXZ[];\n"
    "out vec2 teWorldXZ[];\n"
    "uniform float uTessNear;\n"   /* distance at which we use max subdivision */
    "uniform float uTessFar;\n"
    "uniform float uTessMax;\n"
    "\n"
    "float tessForEdge(vec2 a, vec2 b) {\n"
    "    vec2 mid = (a + b) * 0.5;\n"
    "    float d = distance(uCameraPos.xz, mid);\n"
    "    float t = clamp((d - uTessNear) / max(uTessFar - uTessNear, 1.0), 0.0, 1.0);\n"
    "    return mix(uTessMax, 1.0, t * t);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    teWorldXZ[gl_InvocationID] = tcWorldXZ[gl_InvocationID];\n"
    "    if (gl_InvocationID == 0) {\n"
    /*     A patch with no water under ANY corner is dry: setting its outer
           levels to zero discards the whole patch before it ever reaches the
           rasteriser, which is what lets a river cost only the cells it covers
           instead of the whole map. */
    "        float d0 = waterSampleField(tcWorldXZ[0]).y;\n"
    "        float d1 = waterSampleField(tcWorldXZ[1]).y;\n"
    "        float d2 = waterSampleField(tcWorldXZ[2]).y;\n"
    "        float d3 = waterSampleField(tcWorldXZ[3]).y;\n"
    "        if (max(max(d0, d1), max(d2, d3)) <= 0.0) {\n"
    "            gl_TessLevelOuter[0] = 0.0;\n"
    "            gl_TessLevelOuter[1] = 0.0;\n"
    "            gl_TessLevelOuter[2] = 0.0;\n"
    "            gl_TessLevelOuter[3] = 0.0;\n"
    "            gl_TessLevelInner[0] = 0.0;\n"
    "            gl_TessLevelInner[1] = 0.0;\n"
    "            return;\n"
    "        }\n"
    /*     Shared edges must get identical levels from both patches or cracks
           open along the seam, so each level comes only from that edge's two
           endpoints -- never from the patch centre. */
    "        float e0 = tessForEdge(tcWorldXZ[3], tcWorldXZ[0]);\n"
    "        float e1 = tessForEdge(tcWorldXZ[0], tcWorldXZ[1]);\n"
    "        float e2 = tessForEdge(tcWorldXZ[1], tcWorldXZ[2]);\n"
    "        float e3 = tessForEdge(tcWorldXZ[2], tcWorldXZ[3]);\n"
    "        gl_TessLevelOuter[0] = e0;\n"
    "        gl_TessLevelOuter[1] = e1;\n"
    "        gl_TessLevelOuter[2] = e2;\n"
    "        gl_TessLevelOuter[3] = e3;\n"
    "        gl_TessLevelInner[0] = max(e1, e3);\n"
    "        gl_TessLevelInner[1] = max(e0, e2);\n"
    "    }\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Tessellation evaluation: this is where the surface actually gets its shape.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_tes =
    "layout(quads, fractional_odd_spacing, ccw) in;\n"
    "in vec2 teWorldXZ[];\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "out vec3  vWorldPos;\n"
    "out vec3  vNormal;\n"
    "out vec4  vClip;\n"
    "out float vDepth;\n"
    "out vec2  vFlow;\n"
    "out float vCrest;\n"
    "out float vFold;\n"
    "\n"
    "void main() {\n"
    "    vec2 a = mix(teWorldXZ[0], teWorldXZ[1], gl_TessCoord.x);\n"
    "    vec2 b = mix(teWorldXZ[3], teWorldXZ[2], gl_TessCoord.x);\n"
    "    vec2 p = mix(a, b, gl_TessCoord.y);\n"
    "\n"
    "    vec4 f = waterSampleField(p);\n"
    "    vDepth = f.y;\n"
    "    vFlow  = f.zw;\n"
    "    float shoal = clamp(f.y / 1.5, 0.0, 1.0);\n"
    "    vec3 nrm; float crest;\n"
    "    vec3 disp = waterSurface(p, shoal, nrm, crest, vFold);\n"
    "    vec3 wp = vec3(p.x, f.x, p.y) + disp;\n"
    "    vNormal = nrm;\n"
    "    vCrest = crest;\n"
    "    vWorldPos = wp;\n"
    "    gl_Position = uProjection * uView * vec4(wp, 1.0);\n"
    "    vClip = gl_Position;\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Fragment: the photorealistic surface.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_frag =
    "in vec3  vWorldPos;\n"
    "in vec3  vNormal;\n"
    "in vec4  vClip;\n"
    "in float vDepth;\n"
    "in vec2  vFlow;\n"
    "in float vCrest;\n"
    "in float vFold;\n"
    "out vec4 FragColor;\n"
    "\n"
    "uniform vec3  uSunDir;\n"       /* points FROM the surface TO the sun */
    "uniform vec3  uSunColor;\n"
    "uniform vec3  uAmbient;\n"
    "uniform mat4  uInvViewProj;\n"
    "uniform mat4  uViewProj;\n"
    "uniform vec2  uScreenSize;\n"
    "\n"
    "uniform sampler2D uSceneTex;\n"   /* opaque colour, for refraction + SSR   */
    "uniform sampler2D uDepthTex;\n"   /* opaque depth,  for thickness + SSR    */
    "uniform int   uHasScene;\n"
    "\n"
    "uniform vec3  uAbsorption;\n"     /* per-channel extinction, 1/metre       */
    "uniform vec3  uScatterColor;\n"   /* colour of light scattered back out    */
    "uniform float uRoughness;\n"
    "uniform float uRefractStrength;\n"
    "uniform float uFoamAmount;\n"
    "uniform float uFoamMaxCover;\n"
    "uniform sampler2D uFoamTex;\n"
    "uniform float uOpacity;\n"
    "\n"
    "uniform int   uFogEnabled;\n"
    "uniform vec3  uFogColor;\n"
    "uniform float uFogStart;\n"
    "uniform float uFogEnd;\n"
    "\n"
    "#if WATER_IBL\n"
    "uniform samplerCube uPrefilter;\n"
    "uniform float uPrefilterMips;\n"
    "uniform int   uHasIBL;\n"
    "#endif\n"
    "\n"
    /* --- small helpers --------------------------------------------------- */
    "float hash12(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "\n"
    "float valueNoise(vec2 p) {\n"
    "    vec2 i = floor(p), f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),\n"
    "               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "\n"
    /* World position of the opaque surface behind a screen pixel, from the
       depth buffer. Everything about thickness and shorelines depends on this. */
    "vec3 worldFromDepth(vec2 uv, float rawDepth) {\n"
    "    vec4 ndc = vec4(uv * 2.0 - 1.0, rawDepth * 2.0 - 1.0, 1.0);\n"
    "    vec4 w = uInvViewProj * ndc;\n"
    "    return w.xyz / w.w;\n"
    "}\n"
    "\n"
    /* GGX specular for a single directional light. */
    "float specGGX(vec3 N, vec3 V, vec3 L, float rough) {\n"
    "    vec3 H = normalize(V + L);\n"
    "    float a = max(rough * rough, 0.002);\n"
    "    float a2 = a * a;\n"
    "    float NdotH = max(dot(N, H), 0.0);\n"
    "    float NdotV = max(dot(N, V), 1e-4);\n"
    "    float NdotL = max(dot(N, L), 0.0);\n"
    "    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
    /* Water is very smooth, so a2 is tiny and d collapses to ~0 right on the
       specular peak. Unguarded, D goes to infinity there; multiplying that by a
       light colour with a zero channel yields 0*inf = NaN, which shows up as
       BLACK SPECKLES scattered over the wave crests. Clamp the denominator and
       cap the peak: infinite energy from a point highlight is unphysical anyway,
       and the cap is what stops fireflies. */
    "    float D = a2 / max(3.14159265 * d * d, 1.0e-7);\n"
    "    float k = a * 0.5;\n"
    "    float gv = NdotV / max(NdotV * (1.0 - k) + k, 1.0e-5);\n"
    "    float gl = NdotL / max(NdotL * (1.0 - k) + k, 1.0e-5);\n"
    "    return min(D * gv * gl * NdotL, 40.0);\n"
    "}\n"
    "\n"
    /* Detail ripples. Two layers scrolled along the flow at different rates and
       cross-faded, so a river's surface moves downstream without the texture
       visibly stretching -- the standard flow-map trick. */
    /* Several octaves, each rotated. A SINGLE octave of value noise is a grid of
       rounded blobs -- its lattice is plainly visible once there are no larger
       waves to hide it -- and simply scaling octaves without rotating them keeps
       every lattice aligned, so the grid survives. Rotating each octave by an
       irrational-ish angle breaks the alignment and the result reads as water. */
    "float rippleFBM(vec2 p, float t) {\n"
    "    const mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);\n"
    "    float sum = 0.0, amp = 0.5, norm = 0.0;\n"
    "    vec2 q = p;\n"
    "    for (int i = 0; i < 3; i++) {\n"
    "        sum  += amp * valueNoise(q + vec2(t * (0.13 + 0.07 * float(i)),\n"
    "                                          t * (-0.11 + 0.05 * float(i))));\n"
    "        norm += amp;\n"
    "        q = rot * q * 2.03;\n"
    "        amp *= 0.5;\n"
    "    }\n"
    "    return sum / norm;\n"
    "}\n"
    "\n"
    "vec3 detailNormal(vec2 p, vec2 flow, float strength) {\n"
    "    float t = uTime * 0.5;\n"
    "    float phase0 = fract(t);\n"
    "    float phase1 = fract(t + 0.5);\n"
    "    vec2 fdir = flow * 0.35;\n"
    "    vec2 uv0 = p * 0.35 - fdir * phase0 * 4.0;\n"
    "    vec2 uv1 = p * 0.35 - fdir * phase1 * 4.0;\n"
    "    float w = abs(0.5 - phase0) * 2.0;\n"
    "    vec2 e = vec2(0.06, 0.0);\n"
    "    float n0 = mix(rippleFBM(uv0, uTime), rippleFBM(uv1, uTime), w);\n"
    "    float nx = mix(rippleFBM(uv0 + e.xy, uTime), rippleFBM(uv1 + e.xy, uTime), w);\n"
    "    float nz = mix(rippleFBM(uv0 + e.yx, uTime), rippleFBM(uv1 + e.yx, uTime), w);\n"
    "    return normalize(vec3((n0 - nx) * strength, 0.12, (n0 - nz) * strength));\n"
    "}\n"
    "\n"
    "#if WATER_SSR\n"
    /* Screen-space reflection. Marches the reflected ray in world space,
       projecting each step to compare against the depth buffer. Returns 0 in
       .a when nothing was hit, so the caller can fall back to the sky. */
    "vec4 traceSSR(vec3 origin, vec3 dir) {\n"
    "    const int STEPS = 24;\n"
    "    float stride = 0.6;\n"
    "    vec3 p = origin;\n"
    "    for (int i = 0; i < STEPS; i++) {\n"
    "        p += dir * stride;\n"
    "        stride *= 1.18;\n"           /* coarser as it travels: cheap distance LOD */
    "        vec4 proj = uViewProj * vec4(p, 1.0);\n"
    "        if (proj.w <= 0.0) break;\n"
    "        vec3 ndc = proj.xyz / proj.w;\n"
    "        vec2 uv = ndc.xy * 0.5 + 0.5;\n"
    "        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;\n"
    "        float sceneD = texture(uDepthTex, uv).r;\n"
    "        float rayD = ndc.z * 0.5 + 0.5;\n"
    "        if (rayD > sceneD + 1.0e-5) {\n"
    /*         Hit. Fade near the screen edges, or reflections pop as objects
               leave the frame. */
    "            vec2 edge = smoothstep(vec2(0.0), vec2(0.12), uv)\n"
    "                      * smoothstep(vec2(0.0), vec2(0.12), 1.0 - uv);\n"
    "            float fade = edge.x * edge.y;\n"
    "            return vec4(texture(uSceneTex, uv).rgb, fade);\n"
    "        }\n"
    "    }\n"
    "    return vec4(0.0);\n"
    "}\n"
    "#endif\n"
    "\n"
    "void main() {\n"
    /* Dry ground: the field says there is no water here. */
    "    if (vDepth <= 0.0) discard;\n"
    "\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    "    vec2 screenUV = (vClip.xy / vClip.w) * 0.5 + 0.5;\n"
    "\n"
    /* Ripple detail fades with distance so the surface does not turn into
       aliased noise at the horizon. */
    "    float viewDist = length(uCameraPos - vWorldPos);\n"
    "    float detailFade = clamp(1.0 - viewDist / 220.0, 0.0, 1.0);\n"
    "    float shoal = clamp(vDepth / 1.5, 0.0, 1.0);\n"
    /* The spectral cascades already carry genuine small-scale waves, so the
       procedural ripples only need to fill in below the finest cascade. Leaving
       them at full strength there would bury the FFT detail under noise. */
    "#if WATER_SPECTRUM\n"
    "    float detailAmt = 0.45;\n"
    "#else\n"
    "    float detailAmt = 1.6;\n"
    "#endif\n"
    "    vec3 dn = detailNormal(vWorldPos.xz, vFlow, detailAmt * detailFade * mix(0.4, 1.0, shoal));\n"
    "    vec3 base = vNormal;\n"
    "#if WATER_SPECTRUM\n"
    /* Rebuild the wave normal here rather than using the interpolated one: the
       automatic mip level is chosen per pixel, so distant water averages its
       waves instead of flickering between them. */
    "    vec2 sl = waterSpectrumSlopeAuto(vWorldPos.xz, shoal);\n"
    "    base = normalize(vec3(-sl.x, 1.0, -sl.y));\n"
    "#endif\n"
    "    vec3 N = normalize(base + vec3(dn.x, 0.0, dn.z));\n"
    "    if (dot(N, V) < 0.0) N = reflect(N, V);\n"   /* never shade a back-face */
    /* Averaging the normal threw away sub-pixel wave detail; that detail was
       spreading the highlight, so put the energy back as roughness. Without
       this the distant sea turns into a mirror and the sun glint aliases into
       hard sparkling dots -- the classic specular-aliasing giveaway. */
    "    float roughDist = uRoughness + 0.28 * (1.0 - detailFade);\n"
    "\n"
    /* --- how much water the view ray travels through ---------------------- */
    "    float column = vDepth;\n"
    "    vec3 bottomWorld = vWorldPos - vec3(0.0, vDepth, 0.0);\n"
    "    if (uHasScene != 0) {\n"
    "        float sceneD = texture(uDepthTex, screenUV).r;\n"
    "        bottomWorld = worldFromDepth(screenUV, sceneD);\n"
    /*     Distance along the VIEW RAY, not straight down: at a grazing angle you
           are looking through far more water than the vertical depth, which is
           exactly why a shallow sea still reads as deep blue toward the horizon. */
    "        column = max(length(bottomWorld - vWorldPos), 0.0);\n"
    "    }\n"
    "\n"
    /* --- refraction ------------------------------------------------------- */
    "    vec3 refracted;\n"
    "    float bendScale = clamp(column * 0.12, 0.0, 1.0) * uRefractStrength;\n"
    "    vec2 refrUV = screenUV + N.xz * bendScale * 0.06;\n"
    "    float usedColumn = column;\n"
    "    if (uHasScene != 0) {\n"
    "        float rd = texture(uDepthTex, refrUV).r;\n"
    "        vec3 rw = worldFromDepth(refrUV, rd);\n"
    /*     If the offset lands on something IN FRONT of the water, that pixel is
           not underwater and bending it in would smear a foreground object
           across the surface. Fall back to the straight sample. */
    "        if (dot(rw - vWorldPos, V) > 0.0) {\n"
    "            refrUV = screenUV;\n"
    "        } else {\n"
    "            usedColumn = max(length(rw - vWorldPos), 0.0);\n"
    "        }\n"
    "        refracted = texture(uSceneTex, refrUV).rgb;\n"
    "    } else {\n"
    "        refracted = uScatterColor;\n"
    "    }\n"
    "\n"
    /* --- Beer-Lambert absorption ------------------------------------------ */
    /* Water does not have a "colour"; it absorbs red first, then green, and
       blue last. Letting the depth drive per-channel extinction is what gives
       the real turquoise-to-deep-blue gradient instead of a flat blue tint. */
    "    vec3 T = exp(-uAbsorption * usedColumn);\n"
    "    vec3 body = refracted * T + uScatterColor * (1.0 - T);\n"
    "\n"
    /* --- reflection -------------------------------------------------------- */
    "    vec3 R = reflect(-V, N);\n"
    "    R.y = abs(R.y);\n"                 /* keep it out of the ground */
    "    vec3 reflection = uScatterColor * 1.5 + uAmbient;\n"
    "#if WATER_IBL\n"
    "    if (uHasIBL != 0) {\n"
    "        float lod = roughDist * uPrefilterMips;\n"
    "        reflection = textureLod(uPrefilter, R, lod).rgb;\n"
    "    }\n"
    "#endif\n"
    "#if WATER_SSR\n"
    "    if (uHasScene != 0) {\n"
    "        vec4 ssr = traceSSR(vWorldPos, R);\n"
    "        reflection = mix(reflection, ssr.rgb, ssr.a);\n"
    "    }\n"
    "#endif\n"
    "\n"
    /* --- Fresnel ----------------------------------------------------------- */
    /* F0 = 0.02 is water's real reflectance at normal incidence. It is why water
       looks transparent underfoot and mirror-like toward the horizon. */
    "    float NdotV = max(dot(N, V), 0.0);\n"
    "    float F = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);\n"
    "\n"
    /* --- sun specular + subsurface scattering ------------------------------ */
    "    vec3 L = normalize(uSunDir);\n"
    "    float spec = specGGX(N, V, L, max(roughDist, 0.02));\n"
    "    vec3 specular = uSunColor * spec * 2.0;\n"
    "\n"
    /* Light that entered the wave, bounced around inside and came back out
       toward the viewer. Strongest looking INTO the sun through a raised crest,
       which is the glow that makes waves look like water and not glass. */
    "    float sss = pow(clamp(dot(V, -normalize(L + N * 0.6)), 0.0, 1.0), 4.0);\n"
    "    sss *= clamp(vCrest * 0.5 + 0.5, 0.0, 1.0) * shoal;\n"
    "    vec3 scatter = uScatterColor * uSunColor * sss * 1.4;\n"
    "\n"
    "    vec3 color = mix(body, reflection, F) + specular + scatter;\n"
    "\n"
    /* --- foam --------------------------------------------------------------- */
    /* Two sources: the thin rim where water meets solid geometry, and the
       crests of steep waves. Both are modulated by noise so the line never
       reads as a clean geometric band. */
    "    float foamNoise = valueNoise(vWorldPos.xz * 1.6 + vec2(uTime * 0.25, uTime * 0.17));\n"
    /* Shoreline foam comes from TWO independent measures of shallowness, and the
       stronger wins.
        - the field's own water depth, which is always available and is what
          actually defines the shore;
        - the thickness of water along the view ray, which additionally catches
          foam against anything else standing in the water (rocks, hulls, piers).
       Relying only on the second, as this did at first, means no shoreline foam
       at all whenever the scene depth capture is unavailable. */
    /* A shore is where the water EDGE is, and the edge is where depth changes
       fast -- not merely where the water happens to be thin. Keying foam on
       thinness alone paints an entire flooded plain white, because a shallow
       sheet spread over flat ground is thin everywhere. Sampling the depth a
       little away and taking the gradient distinguishes the two: it is large at
       a real waterline and near zero across a uniform film. */
    "    float dHere = vDepth;\n"
    "    vec4 fX = waterSampleField(vWorldPos.xz + vec2(2.0, 0.0));\n"
    "    vec4 fZ = waterSampleField(vWorldPos.xz + vec2(0.0, 2.0));\n"
    "    float dX = fX.y, dZ = fZ.y;\n"
    "    float depthGrad = (abs(dX - dHere) + abs(dZ - dHere)) * 0.5;\n"
    "    float atEdge = smoothstep(0.05, 0.45, depthGrad);\n"
    "    float rimField = 1.0 - clamp(vDepth / 0.9, 0.0, 1.0);\n"
    "    float shoreFoam = smoothstep(0.18, 0.85, rimField * (0.7 + 0.5 * foamNoise)) * atEdge;\n"
    "    if (uHasScene != 0) {\n"
    "        float rim = 1.0 - clamp(column / 1.1, 0.0, 1.0);\n"
    "        shoreFoam = max(shoreFoam,\n"
    "                        smoothstep(0.25, 0.95, rim * (0.65 + 0.5 * foamNoise)) * atEdge);\n"
    "    }\n"
    /* Surf rolls along the shore rather than sitting still. Driven by the noise
       field, NOT by a plain sine of world position: a clean sine is coherent
       across the whole map and shows up as regular parallel stripes over every
       water surface in the scene. */
    "    float surge = valueNoise(vWorldPos.xz * 0.12 - vec2(uTime * 0.35, uTime * 0.2));\n"
    "    shoreFoam *= 0.55 + 0.75 * surge;\n"
    /* Breaking surf. A wave topples when its height passes roughly 0.78 of the
       water depth, so the white line appears where the crest is large compared
       with what is left under it -- which is why breakers form on the bar and
       not out at sea, and why they hug the shoreline's shape. */
    "    float surfFoam = 0.0;\n"
    "    if (uSurfAmount > 0.0 && vDepth > 0.0) {\n"
    "        float ph = beachPhaseAt(vWorldPos.xz, vDepth, uTime);\n"
    "        float band = sin(ph * 6.2831853);\n"
    "        float crest = max(band, 0.0);\n"
    /*     Shoaling: the same energy in less depth means a taller wave. */
    "        float shoalAmp = uSurfAmount / sqrt(max(vDepth, 0.5));\n"
    "        float ratio = (crest * shoalAmp) / max(vDepth, 0.25);\n"
    /*     Foam only on the CREST, and only in water deep enough to still hold a
           wave. Testing the breaker ratio alone paints every shallow shelf solid
           white: the shoaling amplitude keeps climbing as the depth falls, so the
           ratio is over the threshold absolutely everywhere shoreward of the bar.
           Real breakers do not do that -- once a wave breaks it dissipates and
           its height stays pinned near 0.78 of the depth -- so the white has to
           be a LINE riding the crest, not a fill. Below half a unit the swash
           pass owns the water anyway. */
    "        float breaking = smoothstep(0.6, 1.0, ratio);\n"
    "        float tail = smoothstep(-0.35, 0.6, band);\n"
    "        surfFoam = breaking * mix(0.35, 1.0, tail) * (0.7 + 0.5 * foamNoise);\n"
    "    }\n"
    "    float crestFoam = smoothstep(0.86, 1.08, vCrest * (0.7 + 0.5 * foamNoise));\n"
    "    crestFoam = max(crestFoam, surfFoam);\n"
    /* Foam from the Jacobian: where the horizontal displacement has folded the
       surface over itself, the wave is physically breaking. This tracks the real
       geometry rather than a height threshold, so foam appears on the faces that
       are actually pitching forward instead of on every high point. */
    "    float breakFoam = smoothstep(0.45, 1.05, vFold * (0.75 + 0.5 * foamNoise));\n"
    "    float persist = texture(uFoamTex, waterFieldUV(vWorldPos.xz)).r;\n"
    /* The remembered foam is the dominant term where a wave has already gone
       through; the instantaneous ones only add the sharp edges on top. */
    "    float cov = 1.0 - (1.0 - clamp(shoreFoam,  0.0, 1.0))\n"
    "                    * (1.0 - clamp(crestFoam,  0.0, 1.0))\n"
    "                    * (1.0 - clamp(breakFoam,  0.0, 1.0))\n"
    "                    * (1.0 - clamp(persist * 0.45, 0.0, 1.0));\n"
    "    float foam = min(cov * uFoamAmount, uFoamMaxCover);\n"
    "    vec3 foamColor = (uSunColor * 0.5 + uAmbient) * (0.85 + 0.15 * foamNoise);\n"
    "    color = mix(color, foamColor, foam);\n"
    "\n"
    /* --- opacity ------------------------------------------------------------ */
    /* Thin water at the shore must fade out, or the sheet ends in a hard line
       on the sand. Foam and glancing angles both push it back to opaque. */
    "    float alpha = clamp(column / 0.45, 0.0, 1.0);\n"
    "    alpha = max(alpha, F);\n"
    "    alpha = max(alpha, foam);\n"
    /* Step aside where the water is FALLING. A height field has to represent a
       cliff as an almost vertical ramp, and the waterfall pass already hangs a
       proper curtain there; drawing both stacks two transparent surfaces on the
       same pixels, which washes the colour out and leaves seams where the panels
       overlap. A very steep water SURFACE is the tell-tale, so fade it out and
       let the curtain own the drop. */
    /* Only WET neighbours count. In a dry cell the field texture stores the
       GROUND height, not a water level, so comparing against one measures the
       bank rather than the water -- and beside any shore with relief that reads
       as a huge drop and fades the water away. With ponds among hills nearly
       every pixel is within a couple of units of high dry ground, which made the
       water vanish completely. */
    "    float lg = (abs(fX.x - vWorldPos.y) + abs(fZ.x - vWorldPos.y)) * 0.5;\n"
    "    alpha *= 1.0 - 0.9 * smoothstep(1.2, 3.5, lg);\n"
    "    alpha *= uOpacity;\n"
    "    if (uHasScene == 0) alpha = max(alpha, 0.7);\n"
    "\n"
    /* --- fog ---------------------------------------------------------------- */
    "    if (uFogEnabled != 0) {\n"
    "        float fogT = clamp((viewDist - uFogStart) / max(uFogEnd - uFogStart, 0.001), 0.0, 1.0);\n"
    "        color = mix(color, uFogColor, fogT);\n"
    "    }\n"
    "\n"
    "    FragColor = vec4(color, alpha);\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Waterfalls.
   A height field cannot express falling water: a cliff becomes a steep ramp one
   cell wide, which reads as water sliding down a slope. Where the field detects
   a real drop it emits a VERTICAL sheet, drawn with this shader instead of the
   surface one -- falling water is not a surface seen from above but a curtain
   seen edge-on, so it wants vertical streaks, foam that builds on the way down,
   and mist where it lands, none of which the surface shader has any notion of.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_fall_vert =
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec3 attribs;\n"   /* u across, v down, fall height */
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "out vec3  vWorldPos;\n"
    "out vec2  vUV;\n"
    "out float vFallHeight;\n"
    "void main() {\n"
    "    vWorldPos = position;\n"
    "    vUV = attribs.xy;\n"
    "    vFallHeight = attribs.z;\n"
    "    gl_Position = uProjection * uView * vec4(position, 1.0);\n"
    "    gl_Position.z -= 0.0001 * gl_Position.w;\n"   /* keep it off the cliff face */
    "}\n";

static const char *g3d_water_glsl_fall_frag =
    "in vec3  vWorldPos;\n"
    "in vec2  vUV;\n"
    "in float vFallHeight;\n"
    "out vec4 FragColor;\n"
    "uniform float uTime;\n"
    "uniform vec3  uCameraPos;\n"
    "uniform vec3  uSunColor;\n"
    "uniform vec3  uAmbient;\n"
    "uniform vec3  uScatterColor;\n"
    "uniform float uFoamAmount;\n"
    "uniform float uFoamMaxCover;\n"
    "uniform sampler2D uFoamTex;\n"
    "uniform float uMist;\n"
    "uniform int   uFogEnabled;\n"
    "uniform vec3  uFogColor;\n"
    "uniform float uFogStart;\n"
    "uniform float uFogEnd;\n"
    "\n"
    "float hash12(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "float valueNoise(vec2 p) {\n"
    "    vec2 i = floor(p), f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),\n"
    "               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    float drop = max(vFallHeight, 0.5);\n"
    /* Water accelerates as it falls, so the streaks must too -- a constant
       scroll speed is the giveaway that reads as a moving texture rather than
       falling water. Distance under gravity goes as t^2, hence the sqrt. */
    "    float fallSpeed = sqrt(2.0 * 9.81 * drop) * 0.25;\n"
    "    float v = vUV.y;\n"
    /* vUV.x is a WORLD coordinate across the fall, so the streaks run
       continuously over every sheet that makes up a wide curtain. */
    "    vec2 uv = vec2(vUV.x, v * drop - uTime * fallSpeed);\n"
    "\n"
    /* Streaks: noise stretched hard along the fall direction. */
    "    float s1 = valueNoise(vec2(uv.x * 1.7, uv.y * 0.6));\n"
    "    float s2 = valueNoise(vec2(uv.x * 3.9, uv.y * 1.1) + 31.7);\n"
    "    float streak = s1 * 0.65 + s2 * 0.35;\n"
    "\n"
    /* The sheet breaks up as it descends: coherent at the lip, aerated toward
       the bottom. Deliberately short of saturating -- letting foam reach 1 turns
       the whole lower half into flat white, and several sheets seen through one
       another then blow out completely. */
    "    float breakup = smoothstep(0.15, 1.0, v);\n"
    "    float foam = clamp((streak * 0.55 + 0.25) * breakup * uFoamAmount, 0.0, 0.8);\n"
    "    foam = max(foam, smoothstep(0.8, 1.0, v) * 0.55 * uFoamAmount);\n"
    "\n"
    "    vec3 light = uSunColor * 0.75 + uAmbient;\n"
    "    vec3 body = uScatterColor * (0.7 + 0.6 * streak) + light * 0.18;\n"
    "    vec3 color = mix(body, light * (0.85 + 0.15 * streak), foam);\n"
    "\n"
    /* Mist where it lands: a soft glow at the foot of the sheet. */
    "    float mist = smoothstep(0.82, 1.0, v) * uMist;\n"
    "    color = mix(color, light * 0.95, mist * 0.6);\n"
    "\n"
    /* Thin and see-through at the lip, dense and white further down; the mist
       fades out again so the base does not end on a hard edge. */
    /* Kept well below opaque on purpose: a tall cascade is many sheets deep
       along the view ray, and near-opaque ones stack into a flat white wall. */
    "    float alpha = mix(0.28, 0.72, breakup);\n"
    "    alpha = max(alpha, foam * 0.8);\n"
    "    alpha *= 1.0 - smoothstep(0.93, 1.0, v) * 0.55;\n"
    "    alpha = clamp(alpha * (0.75 + 0.35 * streak), 0.0, 1.0);\n"
    "\n"
    "    if (uFogEnabled != 0) {\n"
    "        float d = length(uCameraPos - vWorldPos);\n"
    "        float fogT = clamp((d - uFogStart) / max(uFogEnd - uFogStart, 0.001), 0.0, 1.0);\n"
    "        color = mix(color, uFogColor, fogT);\n"
    "    }\n"
    "    FragColor = vec4(color, alpha);\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Caustics: the moving net of light on everything under the water.
   A full-screen pass, so no other shader in the engine has to know about it --
   it recovers each pixel's world position from the depth buffer, asks the field
   whether that point is submerged, and adds light where the surface above is
   focusing it. Compiled with the same common block, so it samples the field and
   the spectrum exactly the way the surface does.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_caustics_vert =
    "out vec2 vUV;\n"
    /* Full-screen triangle from gl_VertexID: no vertex buffer needed. */
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    vUV = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *g3d_water_glsl_caustics_frag =
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uDepthTex;\n"
    "uniform mat4  uInvViewProj;\n"
    "uniform vec3  uSunDir;\n"
    "uniform vec3  uSunColor;\n"
    "uniform vec3  uAbsorption;\n"
    "uniform float uStrength;\n"
    "uniform float uFlipY;\n"
    "\n"
    "float chash(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "float cnoise(vec2 p) {\n"
    "    vec2 i = floor(p), f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(chash(i), chash(i + vec2(1, 0)), f.x),\n"
    "               mix(chash(i + vec2(0, 1)), chash(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    float raw = texture(uDepthTex, vUV).r;\n"
    "    if (raw >= 0.9999) discard;\n"          /* sky: nothing to light */
    "\n"
    "    vec2 ndcUV = vUV;\n"
    "    if (uFlipY > 0.5) ndcUV.y = 1.0 - ndcUV.y;\n"
    "    vec4 ndc = vec4(ndcUV * 2.0 - 1.0, raw * 2.0 - 1.0, 1.0);\n"
    "    vec4 wp = uInvViewProj * ndc;\n"
    "    vec3 P = wp.xyz / wp.w;\n"
    "\n"
    "    vec4 f = waterSampleField(P.xz);\n"
    "    float level = f.x, wdepth = f.y;\n"
    "    if (wdepth <= 0.0) discard;\n"          /* no water over this point */
    "    float h = level - P.y;\n"
    "    if (h <= 0.02) discard;\n"              /* the point is above the surface */
    "\n"
    /* Walk from the lit point back up to where the sun entered the water. Doing
       this instead of sampling straight overhead is what makes the pattern slide
       correctly as the sun moves, and what puts caustics on vertical faces. */
    "    vec3 L = normalize(uSunDir);\n"
    "    vec3 Lr = refract(-L, vec3(0.0, 1.0, 0.0), 1.0 / 1.333);\n"
    "    vec2 entry = P.xz - Lr.xz * (h / max(-Lr.y, 0.15));\n"
    "\n"
    /* Caustics are cast by the SMALL ripples, not by the swell.
     *
     * The obvious move is to derive them from the FFT cascades, but the spectrum
     * is dominated by long waves: a 40-metre swell is an extremely weak lens, and
     * the fine cascades carry almost no energy (rms 0.0005 against 0.6). Driving
     * the caustics from it yields a flat, invisible result. The fine ripples the
     * viewer actually SEES on the surface come from the procedural detail-normal
     * field, so the light they focus has to come from there too -- otherwise the
     * pattern on the bottom would not match the surface above it.
     *
     * Two drifting layers, and the RIDGES where they cancel give the familiar
     * interlocking web. */
    /* The two layers must differ CLEARLY in scale and orientation. Nearly equal
     * scales (0.42 against 0.47) beat against each other: broad regions appear
     * where both fields almost coincide, |n1-n2| collapses, and the whole area
     * saturates to white -- and because the beat drifts, that blanket sweeps
     * across the sea and the water flashes white every few seconds. Rotating one
     * layer and separating the scales leaves them no way to agree over more than
     * a filament at a time, which is what a caustic web actually is. */
    "    const mat2 rot = mat2(0.6, -0.8, 0.8, 0.6);\n"
    "    vec2 e1 = entry * 0.42 + vec2(uTime * 0.30, uTime * 0.19);\n"
    "    vec2 e2 = entry * 0.47 - vec2(uTime * 0.23, uTime * 0.27);\n"
    "    float n1 = cnoise(e1) + 0.5 * cnoise(e1 * 2.1 + 17.0);\n"
    "    float n2 = cnoise(e2) + 0.5 * cnoise(e2 * 2.3 - 11.0);\n"
    "    float ridge = 1.0 - clamp(abs(n1 - n2) * 3.2, 0.0, 1.0);\n"
    /* Sharpen: real caustics are thin bright filaments, not soft blobs. */
    "    float focus = pow(ridge, 3.0);\n"
    "\n"
    /* Fades out with depth (the pattern blurs away) and in very shallow water
       (there is no lens left to focus anything). */
    /* Caustics need DEPTH to form: the surface is a lens, and a few centimetres
       of water give the refracted rays no distance to converge over. Fading them
       in only below 0.35 left a shallow shelf lit at full strength across its
       whole extent, which reads as a white sea rather than as a caustic web.
       Peak around a metre or two, then absorption takes over. */
    "    float byDepth = exp(-h * 0.16) * smoothstep(0.15, 1.6, h);\n"
    /* Whatever colour the water has already absorbed cannot arrive down here. */
    "    vec3 tint = exp(-uAbsorption * h);\n"
    "    vec3 caustic = uSunColor * tint * (focus * byDepth * uStrength);\n"
    /* Hard ceiling. Caustics only ever ADD light, so nothing downstream can pull
       an over-bright result back; a single bad frame washes the sea out
       completely. Real caustics are a modest brightening over the sea bed. */
    "    // sin tope\n"
    "\n"
    "    FragColor = vec4(caustic, 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Swash: the sheet of water that runs UP the sand after a wave breaks, and the
   dark wet band it leaves as it drains back.
   This cannot live in the surface shader, because there IS no water surface over
   dry sand -- the field says the depth is zero and the fragment is discarded. So
   it is a screen-space pass, like the caustics: recover the sand from the depth
   buffer and decide, per pixel, whether the sea is currently over it.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_swash_frag =
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uDepthTex;\n"
    "uniform mat4  uInvViewProj;\n"
    "uniform vec3  uSunColor;\n"
    "uniform vec3  uAmbient;\n"
    "uniform float uFlipY;\n"
    "\n"
    "float shash(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "float snoise2(vec2 p) {\n"
    "    vec2 i = floor(p), f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(shash(i), shash(i + vec2(1, 0)), f.x),\n"
    "               mix(shash(i + vec2(0, 1)), shash(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    if (uSurfAmount <= 0.0 || uSurfRunup <= 0.0) discard;\n"
    "    float raw = texture(uDepthTex, vUV).r;\n"
    "    if (raw >= 0.9999) discard;\n"
    "\n"
    "    vec2 ndcUV = vUV;\n"
    "    if (uFlipY > 0.5) ndcUV.y = 1.0 - ndcUV.y;\n"
    "    vec4 ndc = vec4(ndcUV * 2.0 - 1.0, raw * 2.0 - 1.0, 1.0);\n"
    "    vec4 wp = uInvViewProj * ndc;\n"
    "    vec3 P = wp.xyz / wp.w;\n"
    "\n"
    "    vec4 f = waterSampleField(P.xz);\n"
    "    if (f.y > 0.02) discard;\n"          /* already under water */
    "\n"
    /* Height above the still water. With a sea, its level is the reference
       everywhere -- and it has to be, because the field texture only carries a
       meaningful surface level for dry cells within a cell or two of the water.
       Past that it stores the ground's own height, so measuring against it gives
       zero everywhere and the swash could never reach more than a metre or so up
       the beach. Without a sea (an inland lake) fall back to the field, and then
       require water genuinely nearby so dry ground elsewhere is not painted. */
    /* The swash only exists NEXT TO the water, so require some within a bounded
       radius. This has to be its own test rather than a consequence of the
       height check: on nearly flat ground every point for miles sits within the
       run-up height of sea level, and keying on height alone whitens the entire
       map once per wave. DEPTH is trustworthy on dry land (it is simply zero) --
       it is the surface LEVEL that is not, which is why the reference height
       still comes from the sea. */
    "    float near = 0.0;\n"
    "    for (int k = 0; k < 8; k++) {\n"
    "        float a = float(k) * 0.7853982;\n"
    "        vec2 dir = vec2(cos(a), sin(a));\n"
    "        near = max(near, waterSampleField(P.xz + dir * 4.0).y);\n"
    "        near = max(near, waterSampleField(P.xz + dir * 11.0).y);\n"
    "    }\n"
    "    // sin acotar\n"
    "\n"
    "    float still = (uSeaLevel > -1.0e29) ? uSeaLevel : f.x;\n"
    "    float above = P.y - still;\n"
    "    if (above < -0.05 || above > uSurfRunup * 1.2) discard;\n"
    "\n"
    /* The tongue of water surges up and drains back. Broken along the shore by
       noise, because a swash edge is ragged, never a clean contour line. */
    "    float ragged = snoise2(P.xz * 0.13) * 0.35 + snoise2(P.xz * 0.42) * 0.15;\n"
    "    float surge = 0.5 + 0.5 * sin(beachPhaseAt(P.xz, 0.0, uTime) * 6.2831853);\n"
    "    float reach = uSurfRunup * (0.25 + 0.75 * surge) * (0.75 + ragged);\n"
    "\n"
    "    float covered = 1.0 - smoothstep(reach * 0.75, reach, above);\n"
    "    if (covered <= 0.001) discard;\n"
    "\n"
    /* Foam gathers at the leading lip of the tongue, and the sand it has just
       left stays dark and wet for a moment. */
    "    float lip = smoothstep(reach * 0.55, reach * 0.95, above) * covered;\n"
    "    vec3 light = uSunColor * 0.6 + uAmbient;\n"
    "    vec3 foam = light * (0.9 + 0.2 * snoise2(P.xz * 1.7 + uTime * 0.6));\n"
    "    vec3 wet = vec3(0.06, 0.09, 0.11);\n"
    "\n"
    "    vec3 col = mix(wet, foam, lip);\n"
    /* Well short of opaque. Wet sand is darker sand, not paint, and the foam is
       a thin lace over it -- at full opacity the beach turns into a white sheet
       every time a wave comes in. */
    "    float a = covered * mix(0.40, 0.72, lip) * clamp(uSurfAmount, 0.0, 1.0);\n"
    "    FragColor = vec4(col, a);\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Underwater: what the world looks like from below the surface.
   Shares the caustics vertex stage. Above water the surface shader already
   applies absorption to whatever is seen THROUGH it, so this only runs when the
   camera itself is submerged -- otherwise the two would be applied twice to the
   same pixels.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_underwater_frag =
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uDepthTex;\n"
    "uniform mat4  uInvViewProj;\n"
    "uniform vec3  uSunDir;\n"
    "uniform vec3  uSunColor;\n"
    "uniform vec3  uAbsorption;\n"
    "uniform vec3  uScatterColor;\n"
    "uniform float uFlipY;\n"
    "uniform float uVisibility;\n"   /* how far you can see, world units */
    "uniform float uShafts;\n"
    "\n"
    "float uhash(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "float unoise(vec2 p) {\n"
    "    vec2 i = floor(p), f = fract(p);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(uhash(i), uhash(i + vec2(1, 0)), f.x),\n"
    "               mix(uhash(i + vec2(0, 1)), uhash(i + vec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    float raw = texture(uDepthTex, vUV).r;\n"
    "    vec2 ndcUV = vUV;\n"
    "    if (uFlipY > 0.5) ndcUV.y = 1.0 - ndcUV.y;\n"
    "    vec4 ndc = vec4(ndcUV * 2.0 - 1.0, raw * 2.0 - 1.0, 1.0);\n"
    "    vec4 wp = uInvViewProj * ndc;\n"
    "    vec3 P = wp.xyz / wp.w;\n"
    "\n"
    /* Nothing was drawn here, so the ray runs to the visibility limit rather
       than to a surface -- otherwise open water would stay perfectly clear. */
    "    float dist = (raw >= 0.9999) ? uVisibility * 1.5 : distance(P, uCameraPos);\n"
    "\n"
    /* Beer-Lambert along the view ray. Per channel, so red disappears within a
       few units and the far distance goes blue-green -- the single strongest cue
       that the viewer is underwater rather than looking at a blue filter. */
    "    vec3 T = exp(-uAbsorption * dist * (12.0 / max(uVisibility, 1.0)));\n"
    "    vec3 fogged = uScatterColor * (1.0 - T);\n"
    "\n"
    /* Shafts of sunlight. The pattern rides the same drifting noise as the
       caustics, so the light on the bottom and the beams in the water agree. */
    "    float shaft = 0.0;\n"
    "    if (uShafts > 0.0) {\n"
    "        vec3 V = normalize(P - uCameraPos);\n"
    "        float toSun = max(dot(V, normalize(uSunDir)), 0.0);\n"
    "        vec2 sp = P.xz * 0.09 + vec2(uTime * 0.05, uTime * 0.03);\n"
    "        float n = unoise(sp) * 0.65 + unoise(sp * 2.3 + 9.0) * 0.35;\n"
    "        shaft = pow(toSun, 6.0) * smoothstep(0.45, 0.9, n)\n"
    "              * clamp(dist / max(uVisibility, 1.0), 0.0, 1.0) * uShafts;\n"
    "    }\n"
    "\n"
    /* Alpha carries the average opacity; the colour carries the tint. Doing the
       per-channel part inside the colour keeps this to one ordinary blend. */
    "    float a = clamp((1.0 - dot(T, vec3(0.3333))) , 0.0, 0.96);\n"
    "    vec3 col = fogged / max(1.0 - dot(T, vec3(0.3333)), 0.001);\n"
    "    col += uSunColor * shaft * 0.35;\n"
    "    FragColor = vec4(col, a);\n"
    "}\n";

/* ---------------------------------------------------------------------------
   Persistent foam.
   Foam that is purely a function of the wave phase appears and vanishes with the
   wave, and that is most of why shader water reads as shader water: real foam
   REMEMBERS. A wave breaks, leaves a white raft behind, the current stretches it
   out and it takes many seconds to dissolve. This keeps a field of it -- carried
   by the same flow the simulation already computes, decaying, and topped up
   wherever the surface is breaking.
   --------------------------------------------------------------------------- */
static const char *g3d_water_glsl_foam_comp =
    "layout(local_size_x = 16, local_size_y = 16) in;\n"
    "layout(r16f, binding = 0) writeonly uniform image2D uFoamOut;\n"
    "uniform sampler2D uFoamPrev;\n"
    "uniform int   uN;\n"
    "uniform float uDt;\n"
    "uniform float uDecay;\n"
    "\n"
    "void main() {\n"
    "    ivec2 id = ivec2(gl_GlobalInvocationID.xy);\n"
    "    if (id.x >= uN || id.y >= uN) return;\n"
    "    vec2 uv = (vec2(id) + 0.5) / float(uN);\n"
    "    vec2 world = uFieldOriginSize.xy + uv * uFieldOriginSize.zw;\n"
    "\n"
    "    vec4 f = texture(uFieldTex, uv);\n"
    "    float depth = f.y;\n"
    "    vec2  flow  = f.zw;\n"
    "\n"
    /* Carried by the current: read from where this water CAME from. Sampling
       backwards along the flow is what makes a raft of foam stretch downstream
       instead of sitting still while the water moves under it. */
    "    vec2 back = uv - (flow * uDt) / uFieldOriginSize.zw;\n"
    "    float prev = texture(uFoamPrev, clamp(back, 0.0, 1.0)).r;\n"
    "\n"
    /* Decays over seconds, not frames, so it looks the same at any frame rate. */
    "    float foam = prev * exp(-uDecay * uDt);\n"
    "\n"
    /* Topped up where the surface is breaking right now. */
    "    if (depth > 0.0) {\n"
    "        float ph = beachPhaseAt(world, depth, uTime);\n"
    "        float crest = max(sin(ph * 6.2831853), 0.0);\n"
    "        float ratio = crest * surfBreakiness(depth);\n"
    "        float born = smoothstep(0.80, 1.25, ratio) * smoothstep(0.15, 0.6, depth);\n"
    "        foam = max(foam, born);\n"
    /*     A little is also churned up in the shallows, where the water is always
           disturbed even between waves. */
    "        foam = max(foam, (1.0 - smoothstep(0.0, 0.8, depth)) * 0.08);\n"
    "    } else {\n"
    "        foam = 0.0;\n"
    "    }\n"
    "    imageStore(uFoamOut, id, vec4(clamp(foam, 0.0, 1.0), 0.0, 0.0, 0.0));\n"
    "}\n";

#endif /* __LIBMOD_3D_WATER_SHADERS_H */
