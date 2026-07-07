/*
 * libmod_3d_primitives.c - Primitive Mesh Generation Implementation
 */

#include "libmod_3d_primitives.h"
#include <stdlib.h>
#include <math.h>

/* ============================================================================
   VALUE NOISE (for procedural terrain)
   ============================================================================
 */

/* Hash a grid cell to a pseudo-random value in [0,1] */
static float g3d_noise_hash(int x, int y, unsigned int seed) {
    unsigned int h = (unsigned int)(x * 374761393 + y * 668265263) ^ seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

/* Bilinearly-interpolated value noise with smoothstep */
static float g3d_value_noise(float x, float y, unsigned int seed) {
    int xi = (int)floorf(x);
    int yi = (int)floorf(y);
    float xf = x - (float)xi;
    float yf = y - (float)yi;

    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);

    float a = g3d_noise_hash(xi, yi, seed);
    float b = g3d_noise_hash(xi + 1, yi, seed);
    float c = g3d_noise_hash(xi, yi + 1, seed);
    float d = g3d_noise_hash(xi + 1, yi + 1, seed);

    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

/* Fractal Brownian motion: sum of noise octaves, returns ~[0,1] */
static float g3d_fbm(float x, float y, unsigned int seed) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < 5; o++) {
        sum += amp * g3d_value_noise(x * freq, y * freq, seed + (unsigned)o * 131u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

/* Ridged multifractal noise: sharp ridges/crags for rocky mountains, ~[0,1] */
static float g3d_ridged(float x, float y, unsigned int seed) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < 6; o++) {
        float n = g3d_value_noise(x * freq, y * freq, seed + (unsigned)o * 197u);
        n = 1.0f - fabsf(2.0f * n - 1.0f); /* fold into a ridge */
        n = n * n;                          /* sharpen */
        sum += amp * n;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

G3DMesh *g3d_primitive_create_cube(void) {
    /* Define cube vertices (12 vertices for 6 faces with normals) */
    G3DVertex vertices[] = {
        /* Front face */
        {{-0.5, -0.5, 0.5}, {0, 0, 1}, {0, 0}},
        {{0.5, -0.5, 0.5}, {0, 0, 1}, {1, 0}},
        {{0.5, 0.5, 0.5}, {0, 0, 1}, {1, 1}},
        {{-0.5, 0.5, 0.5}, {0, 0, 1}, {0, 1}},

        /* Back face */
        {{0.5, -0.5, -0.5}, {0, 0, -1}, {0, 0}},
        {{-0.5, -0.5, -0.5}, {0, 0, -1}, {1, 0}},
        {{-0.5, 0.5, -0.5}, {0, 0, -1}, {1, 1}},
        {{0.5, 0.5, -0.5}, {0, 0, -1}, {0, 1}},

        /* Top face */
        {{-0.5, 0.5, 0.5}, {0, 1, 0}, {0, 0}},
        {{0.5, 0.5, 0.5}, {0, 1, 0}, {1, 0}},
        {{0.5, 0.5, -0.5}, {0, 1, 0}, {1, 1}},
        {{-0.5, 0.5, -0.5}, {0, 1, 0}, {0, 1}},

        /* Bottom face */
        {{-0.5, -0.5, -0.5}, {0, -1, 0}, {0, 0}},
        {{0.5, -0.5, -0.5}, {0, -1, 0}, {1, 0}},
        {{0.5, -0.5, 0.5}, {0, -1, 0}, {1, 1}},
        {{-0.5, -0.5, 0.5}, {0, -1, 0}, {0, 1}},

        /* Right face */
        {{0.5, -0.5, 0.5}, {1, 0, 0}, {0, 0}},
        {{0.5, -0.5, -0.5}, {1, 0, 0}, {1, 0}},
        {{0.5, 0.5, -0.5}, {1, 0, 0}, {1, 1}},
        {{0.5, 0.5, 0.5}, {1, 0, 0}, {0, 1}},

        /* Left face */
        {{-0.5, -0.5, -0.5}, {-1, 0, 0}, {0, 0}},
        {{-0.5, -0.5, 0.5}, {-1, 0, 0}, {1, 0}},
        {{-0.5, 0.5, 0.5}, {-1, 0, 0}, {1, 1}},
        {{-0.5, 0.5, -0.5}, {-1, 0, 0}, {0, 1}},
    };

    uint32_t indices[] = {
        /* Front face */
        0, 1, 2, 2, 3, 0,
        /* Back face */
        4, 5, 6, 6, 7, 4,
        /* Top face */
        8, 9, 10, 10, 11, 8,
        /* Bottom face */
        12, 13, 14, 14, 15, 12,
        /* Right face */
        16, 17, 18, 18, 19, 16,
        /* Left face */
        20, 21, 22, 22, 23, 20,
    };

    return g3d_mesh_create("Cube", vertices, 24, indices, 36);
}

G3DMesh *g3d_primitive_create_sphere(int segments) {
    if (segments < 3)
        segments = 3;

    int vertex_count = (segments + 1) * (segments + 1);
    int index_count = segments * segments * 6;

    G3DVertex *vertices = (G3DVertex *)malloc(vertex_count * sizeof(G3DVertex));
    uint32_t *indices = (uint32_t *)malloc(index_count * sizeof(uint32_t));

    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        return NULL;
    }

    /* Generate sphere vertices */
    int v_idx = 0;
    for (int i = 0; i <= segments; i++) {
        float v = (float)i / segments;
        float theta = v * 3.14159265f;

        for (int j = 0; j <= segments; j++) {
            float u = (float)j / segments;
            float phi = u * 2 * 3.14159265f;

            float x = cosf(phi) * sinf(theta);
            float y = cosf(theta);
            float z = sinf(phi) * sinf(theta);

            vertices[v_idx].position[0] = x;
            vertices[v_idx].position[1] = y;
            vertices[v_idx].position[2] = z;

            vertices[v_idx].normal[0] = x;
            vertices[v_idx].normal[1] = y;
            vertices[v_idx].normal[2] = z;

            vertices[v_idx].texcoord[0] = u;
            vertices[v_idx].texcoord[1] = v;

            v_idx++;
        }
    }

    /* Generate indices */
    int idx = 0;
    for (int i = 0; i < segments; i++) {
        for (int j = 0; j < segments; j++) {
            uint32_t a = i * (segments + 1) + j;
            uint32_t b = a + 1;
            uint32_t c = a + (segments + 1);
            uint32_t d = c + 1;

            indices[idx++] = a;
            indices[idx++] = c;
            indices[idx++] = b;

            indices[idx++] = b;
            indices[idx++] = c;
            indices[idx++] = d;
        }
    }

    return g3d_mesh_create("Sphere", vertices, vertex_count, indices,
                           index_count);
}

G3DMesh *g3d_primitive_create_plane(void) {
    G3DVertex vertices[] = {
        {{-0.5, 0, -0.5}, {0, 1, 0}, {0, 0}},
        {{0.5, 0, -0.5}, {0, 1, 0}, {1, 0}},
        {{0.5, 0, 0.5}, {0, 1, 0}, {1, 1}},
        {{-0.5, 0, 0.5}, {0, 1, 0}, {0, 1}},
    };

    uint32_t indices[] = {0, 1, 2, 2, 3, 0};

    return g3d_mesh_create("Plane", vertices, 4, indices, 6);
}

/* Build a terrain mesh from an explicit (side x side) height array. Computes
   normals from neighbouring heights and tiled texcoords; sets terrain metadata. */
static G3DMesh *terrain_build(int side, float world_size, const float *heights,
                             float tiling) {
    if (side < 2)
        return NULL;
    int grid = side - 1;
    int vcount = side * side;
    int icount = grid * grid * 6;

    G3DVertex *verts = (G3DVertex *)malloc((size_t)vcount * sizeof(G3DVertex));
    uint32_t *indices = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!verts || !indices) {
        free(verts);
        free(indices);
        return NULL;
    }

    float cell = world_size / (float)grid;

    for (int j = 0; j < side; j++) {
        for (int i = 0; i < side; i++) {
            int idx = j * side + i;
            float fx = (float)i / (float)grid;
            float fz = (float)j / (float)grid;

            int il = (i > 0) ? i - 1 : i;
            int ir = (i < grid) ? i + 1 : i;
            int jd = (j > 0) ? j - 1 : j;
            int ju = (j < grid) ? j + 1 : j;
            float hl = heights[j * side + il];
            float hr = heights[j * side + ir];
            float hd = heights[jd * side + i];
            float hu = heights[ju * side + i];

            float nx = hl - hr;
            float ny = 2.0f * cell;
            float nz = hd - hu;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len < 1e-6f)
                len = 1.0f;

            verts[idx].position[0] = (fx - 0.5f) * world_size;
            verts[idx].position[1] = heights[idx];
            verts[idx].position[2] = (fz - 0.5f) * world_size;
            verts[idx].normal[0] = nx / len;
            verts[idx].normal[1] = ny / len;
            verts[idx].normal[2] = nz / len;
            verts[idx].texcoord[0] = fx * tiling;
            verts[idx].texcoord[1] = fz * tiling;
        }
    }

    int k = 0;
    for (int j = 0; j < grid; j++) {
        for (int i = 0; i < grid; i++) {
            uint32_t a = (uint32_t)(j * side + i);
            uint32_t b = (uint32_t)(j * side + i + 1);
            uint32_t c = (uint32_t)((j + 1) * side + i);
            uint32_t d = (uint32_t)((j + 1) * side + i + 1);
            indices[k++] = a;
            indices[k++] = c;
            indices[k++] = b;
            indices[k++] = b;
            indices[k++] = c;
            indices[k++] = d;
        }
    }

    G3DMesh *mesh = g3d_mesh_create("Terrain", verts, (uint32_t)vcount, indices,
                                    (uint32_t)icount);
    if (mesh) {
        mesh->terrain_side = side;
        mesh->terrain_world_size = world_size;
    }
    free(verts);
    free(indices);
    return mesh;
}

/* Public wrapper around terrain_build: rebuild a terrain from explicit heights. */
G3DMesh *g3d_primitive_terrain_from_heights(int side, float world_size,
                                            const float *heights, float tiling) {
    return terrain_build(side, world_size, heights, tiling);
}

/*
 * Procedural terrain: a (grid+1)x(grid+1) heightmap on the XZ plane, centered
 * at the origin. Heights come from fractal value noise. Texcoords are scaled by
 * `tiling` so a repeating (GL_REPEAT) texture tiles across the surface.
 */
G3DMesh *g3d_primitive_create_terrain(int grid, float world_size, float height,
                                      float tiling, unsigned int seed) {
    if (grid < 1) grid = 1;
    if (grid > 2048) grid = 2048;   /* allow large/kilometric terrains */
    int side = grid + 1;

    float *heights = (float *)malloc((size_t)side * side * sizeof(float));
    if (!heights)
        return NULL;

    const float noise_scale = 4.0f;
    for (int j = 0; j < side; j++) {
        for (int i = 0; i < side; i++) {
            float fx = (float)i / (float)grid;
            float fz = (float)j / (float)grid;
            heights[j * side + i] =
                g3d_fbm(fx * noise_scale, fz * noise_scale, seed) * height;
        }
    }

    G3DMesh *mesh = terrain_build(side, world_size, heights, tiling);
    free(heights);
    return mesh;
}

/*
 * Mountain: a single radial peak (gaussian-ish) with fbm detail and a carved
 * channel running down the -Z face (a gorge for a waterfall/river). The base is
 * sunk below 0 so water forms a lake around the mountain.
 *
 *   peak        - peak height.
 *   channel     - 0..1 depth of the carved front gorge (0 = none).
 */
G3DMesh *g3d_primitive_create_mountain(int grid, float world_size, float peak,
                                       float tiling, unsigned int seed,
                                       float channel) {
    if (grid < 1) grid = 1;
    if (grid > 250) grid = 250;
    int side = grid + 1;

    float *heights = (float *)malloc((size_t)side * side * sizeof(float));
    if (!heights)
        return NULL;

    for (int j = 0; j < side; j++) {
        for (int i = 0; i < side; i++) {
            float u = (float)i / (float)grid;
            float v = (float)j / (float)grid;
            float dx = u - 0.5f;
            float dz = v - 0.5f;
            float r = sqrtf(dx * dx + dz * dz) * 2.0f; /* 0 center .. ~1 edge */

            /* Radial mask (0 edge .. 1 centre), softened */
            float m = 1.0f - r;
            if (m < 0.0f) m = 0.0f;
            m = m * m * (3.0f - 2.0f * m);

            /* Craggy peak: radial mask modulated by ridged noise for sharp
               ridges + a touch of fbm to break the symmetry */
            float ridge = g3d_ridged(u * 5.0f, v * 5.0f, seed);
            float h = peak * m * (0.40f + 0.85f * ridge);
            h += (g3d_fbm(u * 2.3f, v * 2.3f, seed + 7u) - 0.5f) * peak * 0.18f * m;

            /* Carve a smooth U-shaped chasm down the -Z (front) face along x=0
               for the river to run through (a clean channel, no big steps so the
               water doesn't clip through ledges). */
            if (channel > 0.0f) {
                float gorge = expf(-(dx * dx) / 0.012f); /* width of the chasm */
                float prog = (v < 0.55f) ? (1.0f - v / 0.55f) : 0.0f; /* 0 top..1 front */
                h -= gorge * prog * channel * peak * 0.95f;
            }

            /* Sink the base so a lake surrounds the mountain */
            h -= peak * 0.18f;
            heights[j * side + i] = h;
        }
    }

    G3DMesh *mesh = terrain_build(side, world_size, heights, tiling);
    free(heights);
    return mesh;
}

/*
 * Cliff terrain: like the terrain above, but the noise is pushed through a tanh
 * S-curve so lowlands flatten into basins (where water pools), slopes steepen
 * into cliffs/coasts and highlands form plateaus. `steepness` controls how sharp
 * the cliffs are (1 = gentle, 4-6 = dramatic). `water_floor` (0..1) sinks the
 * basins below 0 so water clearly fills them.
 */
G3DMesh *g3d_primitive_create_cliffs(int grid, float world_size, float height,
                                     float tiling, unsigned int seed,
                                     float steepness, float water_floor) {
    if (grid < 1) grid = 1;
    if (grid > 250) grid = 250;
    if (steepness < 0.1f) steepness = 0.1f;
    int side = grid + 1;

    float *heights = (float *)malloc((size_t)side * side * sizeof(float));
    if (!heights)
        return NULL;

    const float noise_scale = 3.0f;
    float denom = tanhf(steepness);
    if (denom < 1e-4f) denom = 1e-4f;

    for (int j = 0; j < side; j++) {
        for (int i = 0; i < side; i++) {
            float fx = (float)i / (float)grid;
            float fz = (float)j / (float)grid;
            float n = g3d_fbm(fx * noise_scale, fz * noise_scale, seed); /* 0..1 */
            /* S-curve around 0.5: flat basins + steep cliffs + flat plateaus */
            float s = 0.5f + 0.5f * tanhf((n - 0.5f) * steepness * 2.0f) / denom;
            /* Sink the lowlands so water_floor maps below zero -> underwater */
            heights[j * side + i] = (s - water_floor) * height;
        }
    }

    G3DMesh *mesh = terrain_build(side, world_size, heights, tiling);
    free(heights);
    return mesh;
}
