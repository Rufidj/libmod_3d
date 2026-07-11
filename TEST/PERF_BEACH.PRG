// ============================================================================
//  PERF_BEACH.PRG - Test de ESTRES en la playa: barcos flotando, nubes
//  volumetricas, palmeras, cientos de tribus animadas, gaviotas volando y
//  cangrejos andando. Mide FPS con carga alta de draws + skinning.
//
//  La animacion en el motor es POR MODELO (malla compartida), asi que uso
//  varias "manadas": cada manada = 1 instancia del modelo con su propia fase;
//  todas sus criaturas comparten pose pero se mueven por su cuenta. Skinning
//  acotado (nº de manadas), draws altos (nº de criaturas) = estres real.
//
//  WASD + raton = volar,  SHIFT = rapido,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int quit;

    // ---- CONFIG (sube/baja para estresar mas o menos) ----
    int N_TRIBU_HERDS;  int TRIBU_PER_HERD;   // tribus animadas
    int N_CRAB_HERDS;   int CRAB_PER_HERD;    // cangrejos
    int N_GULL_HERDS;   int GULL_PER_HERD;    // gaviotas
    int N_SHIPS;                              // barcos
    int N_PALMS;                              // palmeras

    // ---- manadas (modelo + tiempo de animacion por manada) ----
    int   tribu_m[15]; float tribu_t[15];
    int   crab_m[15];  float crab_t[15];
    int   gull_m[15];  float gull_t[15];
    float dur_tribu; float dur_crab; float dur_gull;

    // palmeras / barco (estaticos, cargados una vez)
    int mpalm1; int mpalm2; int mpalm3; int mship;

    // salida de find land/sea (BennuGD no pasa por referencia comodo)
    float lx; float lz;

    int spawned;   // contador de criaturas para el HUD
END

// Busca un punto de TIERRA seca (deja el resultado en lx/lz).
FUNCTION int land_point()
PRIVATE int t; float px; float pz;
BEGIN
    FOR (t = 0; t < 40; t = t + 1)
        px = -160.0 + rand(0, 320);
        pz = 4.0 + rand(0, 54);
        IF (g3d_scene_terrain_height(px, pz) > 1.5)
            lx = px; lz = pz; RETURN 1;
        END
    END
    lx = 0.0; lz = 30.0; RETURN 0;
END

// Busca un punto de MAR (deja el resultado en lx/lz).
FUNCTION int sea_point()
PRIVATE int t; float px; float pz;
BEGIN
    FOR (t = 0; t < 40; t = t + 1)
        px = -150.0 + rand(0, 300);
        pz = -160.0 + rand(0, 120);
        IF (g3d_scene_terrain_height(px, pz) < -0.5)
            lx = px; lz = pz; RETURN 1;
        END
    END
    lx = 0.0; lz = -90.0; RETURN 0;
END

// --- Una tribu que deambula por la playa ---
PROCESS tribu_walker(int model, float wx, float wz)
PRIVATE int ent; float heading; float spd; float wy; float nx; float nz;
BEGIN
    ent = g3d_model_spawn(scene, model, wx, 0.0, wz, 3.2, 0.0);
    heading = rand(0, 359);
    spd = 0.06 + rand(0, 50) / 1000.0;
    LOOP
        IF (rand(0, 100) < 2) heading = heading + rand(-50, 50); END
        nx = wx + sin(heading * 1000) * spd;
        nz = wz + cos(heading * 1000) * spd;
        wy = g3d_scene_terrain_height(nx, nz);
        IF (wy > 1.2 AND nx > -165.0 AND nx < 165.0 AND nz > 2.0 AND nz < 60.0)
            wx = nx; wz = nz;
        ELSE
            heading = heading + 180;   // agua/borde: media vuelta
        END
        g3d_entity_set_position(ent, wx, wy, wz);
        g3d_entity_set_rotation(ent, 0.0, heading * 1000.0, 0.0);
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_model_despawn(ent);
END

// --- Un cangrejo que anda de lado por la arena ---
PROCESS crab_walker(int model, float wx, float wz)
PRIVATE int ent; float heading; float spd; float wy; float nx; float nz;
BEGIN
    ent = g3d_model_spawn(scene, model, wx, 0.0, wz, 1.4, 0.0);
    heading = rand(0, 359);
    spd = 0.05 + rand(0, 40) / 1000.0;
    LOOP
        IF (rand(0, 100) < 3) heading = heading + rand(-70, 70); END
        nx = wx + sin(heading * 1000) * spd;
        nz = wz + cos(heading * 1000) * spd;
        wy = g3d_scene_terrain_height(nx, nz);
        IF (wy > 0.6 AND wy < 4.0 AND nx > -165.0 AND nx < 165.0 AND nz > 0.0 AND nz < 60.0)
            wx = nx; wz = nz;
        ELSE
            heading = heading + 180;
        END
        g3d_entity_set_position(ent, wx, wy, wz);
        // cangrejo: mira 90 grados girado respecto a su avance (anda de lado)
        g3d_entity_set_rotation(ent, 0.0, (heading + 90.0) * 1000.0, 0.0);
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_model_despawn(ent);
END

// --- Una gaviota volando en circulo con balanceo ---
PROCESS gull(int model, float cx, float cz, float radius, float alt, float phase)
PRIVATE int ent; float a; float gx; float gz; float gy;
BEGIN
    ent = g3d_model_spawn(scene, model, cx, alt, cz, 1.4, 0.0);
    a = phase;
    LOOP
        a = a + 0.5;
        gx = cx + cos(a * 1000) * radius;
        gz = cz + sin(a * 1000) * radius;
        gy = alt + 4.0 * sin((a * 3.0) * 1000);
        g3d_entity_set_position(ent, gx, gy, gz);
        g3d_entity_set_rotation(ent, 0.0, (a + 90.0) * 1000.0, 0.0);
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_model_despawn(ent);
END

// --- Un barco cabeceando y derivando por el mar ---
PROCESS boat(int model, float sx, float sz, float dir)
PRIVATE int ent; float ang; float by; float rollv; float pitchv;
BEGIN
    ent = g3d_model_spawn(scene, model, sx, 0.0, sz, 22.0, dir);
    ang = rand(0, 359);
    LOOP
        ang = ang + 1.4;
        sx = sx + sin(dir * 1000) * 0.03;
        sz = sz + cos(dir * 1000) * 0.03;
        IF (sx < -170.0 OR sx > 170.0 OR sz < -170.0 OR sz > -25.0) dir = dir + 180.0; END
        by     = 0.7 * sin(ang * 1000);
        rollv  = 3.5 * sin((ang * 0.7) * 1000);
        pitchv = 2.5 * sin((ang * 0.5) * 1000);
        g3d_entity_set_position(ent, sx, by, sz);
        g3d_entity_set_rotation(ent, pitchv * 1000.0, dir * 1000.0, rollv * 1000.0);
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_model_despawn(ent);
END

PROCESS main()
PRIVATE
    int light; int i; int h; int k; int m; int kshift;
    float dt; float msens; float spd;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    float cx; float cz;
BEGIN
    set_mode(960, 540);
    set_fps(0, 0);            // FPS sin limite (para medir de verdad)

    // ---- CONFIG ----
    N_TRIBU_HERDS = 6;  TRIBU_PER_HERD = 20;   // 120 tribus
    N_CRAB_HERDS  = 4;  CRAB_PER_HERD  = 15;   // 60 cangrejos
    N_GULL_HERDS  = 6;  GULL_PER_HERD  = 18;   // 108 gaviotas
    N_SHIPS = 4;
    N_PALMS = 24;

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.95, 0.85);
    g3d_light_set_direction(light, -0.45, -0.75, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.32, 0.36, 0.44, 1.0);

    // ---- cielo + nubes volumetricas ----
    g3d_sky_set_gradient(0.32, 0.52, 0.85,  0.80, 0.86, 0.94);
    g3d_sky_set_clouds(0.6, 1.0);                    // capa alta (domo)
    g3d_sky_set_low_clouds(0.55, 60.0, 30.0, 1.0);   // capa baja volumetrica
    g3d_sky_enable(1);

    // ---- pipeline ----
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.15);
    g3d_set_bloom(1, 0.35, 0.9); g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6);
    g3d_water_set_ocean(0.0, 1.0, 1.4);

    // ---- cargar modelos ----
    mpalm1 = g3d_load_gltf("assets/coconut_palm.glb");
    mpalm2 = g3d_load_gltf("assets/curly_palm.glb");
    mpalm3 = g3d_load_gltf("assets/palm.glb");
    mship  = g3d_load_gltf("assets/pirate_ship.glb");

    // manadas de tribus / cangrejos / gaviotas (una instancia por manada)
    FOR (h = 0; h < N_TRIBU_HERDS; h = h + 1)
        tribu_m[h] = g3d_load_gltf("assets/tribu.glb");
        tribu_t[h] = h * 1.6;                 // fase distinta por manada
    END
    dur_tribu = g3d_model_anim_duration(tribu_m[0], 0);
    FOR (h = 0; h < N_CRAB_HERDS; h = h + 1)
        crab_m[h] = g3d_load_gltf("assets/crab.glb");
        crab_t[h] = h * 0.5;
    END
    dur_crab = g3d_model_anim_duration(crab_m[0], 0);
    FOR (h = 0; h < N_GULL_HERDS; h = h + 1)
        gull_m[h] = g3d_load_gltf("assets/gaviota.glb");
        gull_t[h] = h * 1.0;
    END
    dur_gull = g3d_model_anim_duration(gull_m[0], 0);

    // ---- poblar la escena ----
    spawned = 0;

    // palmeras (estaticas) en tierra
    FOR (i = 0; i < N_PALMS; i = i + 1)
        IF (land_point())
            m = mpalm1; IF (i % 3 == 1) m = mpalm2; END IF (i % 3 == 2) m = mpalm3; END
            g3d_model_spawn(scene, m, lx, g3d_scene_terrain_height(lx, lz), lz,
                            10.0 + rand(0, 6), rand(0, 359) * 1000.0);
        END
    END

    // tribus
    FOR (h = 0; h < N_TRIBU_HERDS; h = h + 1)
        FOR (k = 0; k < TRIBU_PER_HERD; k = k + 1)
            land_point();
            tribu_walker(tribu_m[h], lx, lz);
            spawned = spawned + 1;
        END
    END
    // cangrejos
    FOR (h = 0; h < N_CRAB_HERDS; h = h + 1)
        FOR (k = 0; k < CRAB_PER_HERD; k = k + 1)
            land_point();
            crab_walker(crab_m[h], lx, lz);
            spawned = spawned + 1;
        END
    END
    // gaviotas (centros repartidos, radios/altitudes variados)
    FOR (h = 0; h < N_GULL_HERDS; h = h + 1)
        FOR (k = 0; k < GULL_PER_HERD; k = k + 1)
            cx = -120.0 + rand(0, 240);
            cz = -120.0 + rand(0, 160);
            gull(gull_m[h], cx, cz, 12.0 + rand(0, 40), 22.0 + rand(0, 45),
                 rand(0, 359));
            spawned = spawned + 1;
        END
    END
    // barcos
    FOR (i = 0; i < N_SHIPS; i = i + 1)
        sea_point();
        boat(mship, lx, lz, rand(0, 359));
    END

    // ---- camara: vista panoramica alta sobre la orilla, mirando al mar ----
    camx = 0.0; camy = 60.0; camz = 70.0;
    yaw = 180.0; pitch = -28.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 480; y = 270;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "WASD+raton volar - SHIFT rapido - ESC salir");
    write(0, 10, 28, 0, "FPS:");     write_var(0, 60, 28, 0, frame_info.fps);
    write(0, 10, 46, 0, "Criaturas:"); write_var(0, 110, 46, 0, spawned);

    LOOP
        // animar cada manada (skinning una vez por manada) con su fase
        FOR (h = 0; h < N_TRIBU_HERDS; h = h + 1)
            tribu_t[h] = tribu_t[h] + dt;
            g3d_model_animate(tribu_m[h], 0, tribu_t[h], 1);
        END
        FOR (h = 0; h < N_CRAB_HERDS; h = h + 1)
            crab_t[h] = crab_t[h] + dt;
            g3d_model_animate(crab_m[h], 0, crab_t[h], 1);
        END
        FOR (h = 0; h < N_GULL_HERDS; h = h + 1)
            gull_t[h] = gull_t[h] + dt * 1.4;   // gaviotas aletean mas rapido
            g3d_model_animate(gull_m[h], 0, gull_t[h], 1);
        END

        // camara libre
        spd = 1.4; IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = 5.0; END
        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
END
