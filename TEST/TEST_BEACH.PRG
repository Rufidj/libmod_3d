// Mapa procedural con una GRAN PLAYA y mar: las olas ruedan y ROMPEN en la orilla.
//  - terreno de un heightmap procedural (demo_beach.g3dh): tierra alta -> playa -> mar
//  - WATERSIM con mar global a nivel 0 (rellena todo lo que este bajo 0)
//  - swell direccional que crece hacia la orilla (shoaling) + surf rompiente
//  - pipeline: HDR + filmic + bloom + SSAO + reflejos (SSR) del agua
// Camara libre: WASD mover, raton mirar, SPACE/LSHIFT subir/bajar. ESC salir.
// Compile: bgdc TEST_BEACH.PRG   Run: bgdi TEST_BEACH.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light; int gid = 0;
    // camara libre
    float camx = 0.0; float camy = 30.0; float camz = -120.0;
    float yaw = 0.0; float pitch = -10.0;
    float msens = 0.12; float spd = 1.8;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    // puntos de salpicadura (rocas dentro del agua)
    float splx[20]; float splz[20]; int nspl = 0; int spltimer = 0;
    // --- camara cinematica (spline Catmull-Rom) ---
    float kx[24]; float ky[24]; float kz[24];    // posiciones clave
    float lx[24]; float ly[24]; float lz[24];    // objetivos (look-at) clave
    int nk = 17;                                 // numero de fotogramas clave
    float cinu = 0.0; int cine = 1; int kcp = 0; // parametro 0..nk-1, activo, edge tecla C
    int a0; int a1; int a2; int a3; float ut;
    float ccx; float ccy; float ccz; float ltx; float lty; float ltz;
END

// Coloca un modelo (todos sus submeshes) en (px,py,pz), escalado a 'th' de alto y girado.
FUNCTION place_model(int model, float px, float py, float pz, float th, float roty)
PRIVATE
    int n; int j; int sm; int st; int mat; int ent; float mh; float s;
    int snorm; int smetal; int srough;
BEGIN
    IF (model < 0) RETURN; END
    mh = g3d_model_height(model);
    IF (mh <= 0.0) mh = 1.0; END
    s = th / mh;
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        // Mapas PBR (normal/metallic/roughness) si el modelo los trae (p.ej. FBX)
        snorm = g3d_model_submesh_map(model, j, 1);
        smetal = g3d_model_submesh_map(model, j, 2);
        srough = g3d_model_submesh_map(model, j, 3);
        IF (snorm >= 0) g3d_material_set_map(mat, 1, snorm); END
        IF (smetal >= 0) g3d_material_set_map(mat, 2, smetal); END
        IF (srough >= 0) g3d_material_set_map(mat, 3, srough); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, s, s, s);
        g3d_entity_set_position(ent, px, py, pz);
        g3d_entity_set_rotation(ent, 0.0, roty * 1000, 0.0);
    END
END

// Interpolacion Catmull-Rom de 4 puntos de control (movimiento suave y continuo).
FUNCTION float crv(float p0, float p1, float p2, float p3, float tt)
BEGIN
    RETURN 0.5 * ((2.0*p1) + (-p0+p2)*tt
                 + (2.0*p0 - 5.0*p1 + 4.0*p2 - p3)*tt*tt
                 + (-p0 + 3.0*p1 - 3.0*p2 + p3)*tt*tt*tt);
END

PROCESS main()
PRIVATE
    int i; int m; float px; float py; float pz;
    int mpalm1; int mpalm2; int mpalm3; int mship; int mrock; int mtorch;
    int mcrash; int mmario; int mluigi; int mbarrel; int placed_char; int mbuggy;
    float h1; float h2; float h3; float h4;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // NOCHE: luna fria y tenue + ambiente oscuro azulado (la escena la iluminan las antorchas)
    light = g3d_light_create(0, 0.5, 0.6, 0.9);
    g3d_light_set_direction(light, -0.3, -1.0, -0.4);
    g3d_light_set_intensity(light, 0.28);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.05, 0.07, 0.13, 1.0);

    // Cielo de noche + nubes volumetricas en movimiento
    g3d_sky_set_gradient(0.02, 0.03, 0.09,  0.06, 0.08, 0.16);
    g3d_sky_set_clouds(0.7, 1.0);      // capa alta lejana (domo del cielo)
    // capa BAJA volumetrica en espacio-mundo: la puedes atravesar subiendo la camara
    // (cobertura, altitud base, grosor, velocidad). Proyecta sombras sobre la escena.
    g3d_sky_set_low_clouds(0.55, 50.0, 28.0, 1.0);
    g3d_sky_enable(1);

    // Pipeline de render
    g3d_set_hdr(1);
    g3d_set_tonemap(1);            // ACES filmic
    g3d_set_exposure(1.15);
    g3d_set_bloom(1, 0.35, 0.9);
    g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6);
    // Oceano: swell hacia la tierra (+Z), olas altas que rompen en la costa
    g3d_water_set_ocean(0.0, 1.0, 1.4);

    // --- Poblar la escena: palmeras en la tierra, barcos en el mar ---
    mpalm1 = g3d_load_gltf("assets/coconut_palm.glb");
    mpalm2 = g3d_load_gltf("assets/curly_palm.glb");
    mpalm3 = g3d_load_gltf("assets/palm.glb");
    mship  = g3d_load_gltf("assets/pirate_ship.glb");

    // Palmeras esparcidas por la tierra junto a la playa (solo en suelo seco)
    FOR (i = 0; i < 16; i = i + 1)
        px = -170.0 + i * 22.0 + 9.0 * sin(i * 61000);
        pz = 6.0 + 52.0 * abs(sin(i * 97000));
        py = g3d_scene_terrain_height(px, pz);
        IF (py > 1.5)
            m = mpalm1;
            IF (i % 3 == 1) m = mpalm2; END
            IF (i % 3 == 2) m = mpalm3; END
            place_model(m, px, py, pz, 11.0 + 5.0 * abs(sin(i * 37000)), i * 40.0);
        END
    END

    // Barco pirata en el mar (a nivel de agua)
    place_model(mship, -25.0, 0.0, -80.0, 24.0, 35.0);
    place_model(mship,  75.0, 0.0, -120.0, 28.0, -55.0);

    // Rocas: en la orilla y asomando del agua poco profunda
    mrock = g3d_load_gltf("assets/rock.glb");
    FOR (i = 0; i < 10; i = i + 1)
        px = -150.0 + i * 30.0 + 12.0 * sin(i * 53000);
        pz = -22.0 + 30.0 * sin(i * 79000);
        py = g3d_scene_terrain_height(px, pz);
        IF (py < 0.5)
            py = -0.6;                       // roca marina: asoma al ras del agua
            IF (nspl < 20) splx[nspl] = px; splz[nspl] = pz; nspl = nspl + 1; END
        END
        place_model(mrock, px, py, pz, 3.0 + 4.0 * abs(sin(i * 41000)), i * 63.0);
    END

    // Modelos para poblar junto a las antorchas
    mtorch  = g3d_load_gltf("assets/torch_simple.glb");
    mcrash  = g3d_load_gltf("assets/crash.glb");
    mmario  = g3d_load_gltf("assets/mario.glb");
    mluigi  = g3d_load_gltf("assets/luigi.glb");
    mbarrel = g3d_load_gltf("assets/barrel.glb");

    // Buggy (FBX) en la arena. Muestreo la altura del terreno bajo su huella y uso la
    // MAS ALTA (+ un pelin) para que ninguna rueda se hunda en los baches.
    mbuggy = g3d_load_fbx("assets/source/VHC_SC_Buggy_01.fbx");
    px = 20.0; pz = 40.0;
    h1 = g3d_scene_terrain_height(px - 5.0, pz);
    h2 = g3d_scene_terrain_height(px + 5.0, pz);
    h3 = g3d_scene_terrain_height(px, pz - 3.5);
    h4 = g3d_scene_terrain_height(px, pz + 3.5);
    py = h1;
    IF (h2 > py) py = h2; END
    IF (h3 > py) py = h3; END
    IF (h4 > py) py = h4; END
    py = py + 0.2;
    place_model(mbuggy, px, py, pz, 4.0, 40.0);

    // Antorchas a lo largo de la orilla; JUSTO AL LADO de cada una, un personaje o barril
    FOR (i = 0; i < 9; i = i + 1)
        px = -150.0 + i * 36.0;
        pz = 3.0 + 16.0 * abs(sin(i * 71000));
        py = g3d_scene_terrain_height(px, pz);
        IF (py > 1.0)
            place_model(mtorch, px, py, pz, 4.5, i * 25.0);
            g3d_fire_add(px, py + 4.3, pz, 1.3);      // llama en la punta
            placed_char = 0;
            IF (i == 1) place_model(mcrash, px + 5.0, py, pz + 1.5, 5.0, 200.0); placed_char = 1; END
            IF (i == 4) place_model(mmario, px + 5.0, py, pz + 1.5, 5.0, 200.0); placed_char = 1; END
            IF (i == 7) place_model(mluigi, px + 5.0, py, pz + 1.5, 5.5, 200.0); placed_char = 1; END
            IF (placed_char == 0) place_model(mbarrel, px + 4.5, py, pz + 1.5, 2.5, i * 47.0); END
        END
    END

    // --- Fotogramas clave del plano secuencia (pos -> objetivo) ---
    // Barco1 en (-25,0,-80), Barco2 en (75,0,-120). Antorchas en x=-150..138, z~3..19.
    // 0-1 dron sobre los barcos | 2-3 a ras del agua pegado al Barco1 | 4-6 pegado al Barco2
    // 7 transicion | 8-16 recorrido LENTO por todas las antorchas (para ver las sombras)
    kx[0] =  25.0; ky[0] = 85.0; kz[0] =-135.0;   lx[0] =  25.0; ly[0] =  5.0; lz[0] =-100.0;
    kx[1] =  10.0; ky[1] = 45.0; kz[1] =-100.0;   lx[1] = -25.0; ly[1] =  8.0; lz[1] = -82.0;
    kx[2] = -48.0; ky[2] =  7.0; kz[2] = -72.0;   lx[2] = -25.0; ly[2] = 10.0; lz[2] = -82.0;
    kx[3] = -25.0; ky[3] =  6.0; kz[3] = -62.0;   lx[3] = -25.0; ly[3] = 12.0; lz[3] = -86.0;
    kx[4] =  20.0; ky[4] =  6.0; kz[4] = -92.0;   lx[4] =  75.0; ly[4] = 10.0; lz[4] =-118.0;
    kx[5] =  52.0; ky[5] =  6.0; kz[5] =-108.0;   lx[5] =  75.0; ly[5] = 12.0; lz[5] =-122.0;
    kx[6] =  98.0; ky[6] =  6.0; kz[6] =-120.0;   lx[6] =  75.0; ly[6] = 12.0; lz[6] =-122.0;
    kx[7] = 140.0; ky[7] = 15.0; kz[7] = -28.0;   lx[7] = 130.0; ly[7] =  3.0; lz[7] =  12.0;
    kx[8] = 130.0; ky[8] = 12.0; kz[8] =  -8.0;   lx[8] = 130.0; ly[8] =  3.0; lz[8] =  14.0;
    kx[9] =  95.0; ky[9] = 12.0; kz[9] =  -8.0;   lx[9] =  95.0; ly[9] =  3.0; lz[9] =  14.0;
    kx[10]=  60.0; ky[10]= 12.0; kz[10]=  -8.0;   lx[10]=  60.0; ly[10]=  3.0; lz[10]=  14.0;
    kx[11]=  25.0; ky[11]= 12.0; kz[11]=  -8.0;   lx[11]=  25.0; ly[11]=  3.0; lz[11]=  14.0;
    kx[12]= -10.0; ky[12]= 12.0; kz[12]=  -8.0;   lx[12]= -10.0; ly[12]=  3.0; lz[12]=  14.0;
    kx[13]= -45.0; ky[13]= 12.0; kz[13]=  -8.0;   lx[13]= -45.0; ly[13]=  3.0; lz[13]=  14.0;
    kx[14]= -80.0; ky[14]= 12.0; kz[14]=  -8.0;   lx[14]= -80.0; ly[14]=  3.0; lz[14]=  14.0;
    kx[15]=-115.0; ky[15]= 12.0; kz[15]=  -8.0;   lx[15]=-115.0; ly[15]=  3.0; lz[15]=  14.0;
    kx[16]=-150.0; ky[16]= 12.0; kz[16]=  -8.0;   lx[16]=-150.0; ly[16]=  3.0; lz[16]=  14.0;

    g3d_mouse_capture(1);
    x = 480; y = 270;

    write(0, 10, 10, 0, "FPS:");
    write_var(0, 70, 10, 0, frame_info.fps);
    write(0, 10, 28, 0, "PLANO SECUENCIA en marcha  -  C: repetir   F: camara libre (WASD+raton)");

    LOOP
        // Salpicaduras periódicas donde las olas golpean las rocas del mar
        spltimer = spltimer + 1;
        IF (spltimer >= 34)
            FOR (i = 0; i < nspl; i = i + 1)
                g3d_water_splash(splx[i], 0.0, splz[i], 1.3);
            END
            spltimer = 0;
        END

        // Teclas: C = (re)lanzar el plano secuencia, F = camara libre
        IF (key(_C) AND kcp == 0) cine = 1; cinu = 0.0; END
        kcp = key(_C);
        IF (key(_F)) cine = 0; END

        IF (cine == 1)
            // ---- PLANO SECUENCIA: camara sobre el spline ----
            cinu = cinu + 0.005;                  // velocidad del barrido
            IF (cinu > nk - 1) cinu = nk - 1; cine = 0; camx = ccx; camy = ccy; camz = ccz; END
            a1 = cinu; IF (a1 > nk - 2) a1 = nk - 2; END   // segmento (floor)
            ut = cinu - a1;
            a0 = a1 - 1; IF (a0 < 0) a0 = 0; END
            a2 = a1 + 1; IF (a2 > nk - 1) a2 = nk - 1; END
            a3 = a1 + 2; IF (a3 > nk - 1) a3 = nk - 1; END
            ccx = crv(kx[a0], kx[a1], kx[a2], kx[a3], ut);
            ccy = crv(ky[a0], ky[a1], ky[a2], ky[a3], ut);
            ccz = crv(kz[a0], kz[a1], kz[a2], kz[a3], ut);
            ltx = crv(lx[a0], lx[a1], lx[a2], lx[a3], ut);
            lty = crv(ly[a0], ly[a1], ly[a2], ly[a3], ut);
            ltz = crv(lz[a0], lz[a1], lz[a2], lz[a3], ut);
            g3d_camera_set_position(camera, ccx, ccy, ccz);
            g3d_camera_look_at(camera, ltx, lty, ltz, 0.0, 1.0, 0.0);
        ELSE
            // ---- CAMARA LIBRE ----
            g3d_mouse_update();
            yaw = yaw + g3d_mouse_dx() * msens;
            pitch = pitch - g3d_mouse_dy() * msens;
            IF (pitch > 85.0) pitch = 85.0; END
            IF (pitch < -85.0) pitch = -85.0; END
            fwx = sin(yaw * 1000) * cos(pitch * 1000);
            fwy = sin(pitch * 1000);
            fwz = cos(yaw * 1000) * cos(pitch * 1000);
            rgx = cos(yaw * 1000);
            rgz = -sin(yaw * 1000);
            IF (key(_W)) camx = camx + fwx * spd; camy = camy + fwy * spd; camz = camz + fwz * spd; END
            IF (key(_S)) camx = camx - fwx * spd; camy = camy - fwy * spd; camz = camz - fwz * spd; END
            IF (key(_D)) camx = camx + rgx * spd; camz = camz + rgz * spd; END
            IF (key(_A)) camx = camx - rgx * spd; camz = camz - rgz * spd; END
            IF (key(_SPACE)) camy = camy + spd; END
            IF (key(_L_SHIFT)) camy = camy - spd; END
            g3d_camera_set_position(camera, camx, camy, camz);
            g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);
        END

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    g3d_shutdown();
END
