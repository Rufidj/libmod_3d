// Crash nadando/vadeando por el mundo de demo.scene (terreno + lago):
//  - en aguas profundas FLOTA (parcialmente hundido) y se mece con el oleaje
//  - en aguas poco profundas vadea con los pies en el fondo
//  - al moverse en el agua, salpica (anillos de ondas + gotas)
// Unos barriles flotan tambien para ver objetos meciendose en el agua.
// El agua y el terreno salen del .scene; el juego solo consulta su altura.
// Animaciones de crash.glb: 1=saltar, 2=andar, 3=idle.
// Compile: bgdc TEST_SWIM.PRG   Run: bgdi TEST_SWIM.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    MODEL_PATH = "assets/crash.glb";
    IDLE_ANIM = 3;
    WALK_ANIM = 2;
    SPEED   = 0.40;     // velocidad de avance (u/frame)
    FACE_OFF = 0.0;     // ajuste de orientacion del modelo (grados)
    SWIM_SUBMERGE  = 2.6;   // cuanto se hunde el cuerpo al flotar (u)
    BARREL_SUBMERGE = 0.6;  // cuanto se hunde un barril flotando (u)
    BOB_AMP = 0.25;         // amplitud del mecido por el oleaje (u)
END

GLOBAL
    int scene; int camera;
    int model; int nsub; int i;
    int parts[64];
    int gid = 0;

    // barriles flotantes (objetos en el agua)
    int barrel_model;
    int bent[64]; float bbx[64]; float bbz[64]; int bn = 0;

    // estado del personaje
    float px = -15.5; float py = 0.0; float pz = 12.0;
    float facing = 0.0;        // orientacion del modelo (grados)
    float camyaw = 0.0;        // orientacion de camara (grados)
    float msens = 0.12;

    // agua
    float waterlvl = -100000.0;
    float floaty;
    int inwater = 0;
    int splashtimer = 0;
    float bobt = 0.0;          // fase del mecido (grados)
    float worldhalf = 0.0;     // media anchura del mapa (para no salirse)
    float prad = 2.0; float phei = 5.0; float pclimb = 1.5;   // colisión del jugador

    // animacion
    int curanim = 0;
    float atime = 0.0;
    float sc; float h;

    float fwx; float fwz; float rgx; float rgz;
    float mvx; float mvz; float mlen; float gy;
    int moving;

    // --- toggles de post-proceso (teclas 1..4) para comparar en vivo ---
    int f_hdr = 1; int f_bloom = 1; int f_ssao = 1; int f_tone = 1; int f_ssr = 1;
    int k1p = 0; int k2p = 0; int k3p = 0; int k4p = 0; int k5p = 0;
END

// Un barril que flota en el agua en (bx,bz): guarda sus entidades y su posicion
// para mecerlo cada frame.
FUNCTION make_floating_barrel(float bx, float bz)
PRIVATE
    int n; int j; int sm; int st; int mat; int ent; float bh; float bsc;
BEGIN
    IF (barrel_model < 0) RETURN; END
    bh = g3d_model_height(barrel_model);
    IF (bh <= 0.0) bh = 1.0; END
    bsc = 3.0 / bh;
    n = g3d_model_submesh_count(barrel_model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(barrel_model, j);
        st = g3d_model_submesh_texture(barrel_model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        g3d_material_set_metallic(mat, 0.9);      // barril metálico -> refleja el cielo (PBR)
        g3d_material_set_roughness(mat, 0.35);
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, bsc, bsc, bsc);
        IF (bn < 64)
            bent[bn] = ent; bbx[bn] = bx; bbz[bn] = bz; bn = bn + 1;
        END
    END
END

PROCESS main()
PRIVATE
    int sm; int st; int mat; int e;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    // Cargar el mundo entero (terreno + lago) desde el fichero de mapa
    scene = g3d_scene_load("demo.scene");
    IF (scene < 0) say("ERROR cargando demo.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Nivel del agua de la escena (para detectar cuando Crash esta dentro)
    waterlvl = g3d_scene_water_level();
    worldhalf = g3d_voxterrain_worldsize() * 0.5 - 3.0;   // margen para no salirse
    prad = g3d_scene_player_radius(); phei = g3d_scene_player_height(); pclimb = g3d_scene_player_climb();

    // --- Pipeline de render (HDR + bloom + SSAO + tonemap filmic) ---
    g3d_set_hdr(f_hdr);
    g3d_set_tonemap(f_tone);         // 1 = ACES filmic (look de cine)
    g3d_set_exposure(1.15);
    g3d_set_bloom(f_bloom, 0.35, 0.9);
    g3d_set_ssao(f_ssao, 0.5, 1.2);
    g3d_water_set_ssr(f_ssr, 0.65);   // reflejos del lago (montañas/cielo/personaje)
    g3d_water_set_ocean(1.0, 0.25, 0.45);   // swell + olas rompiendo en la orilla (playa)

    // Crash
    model = g3d_load_gltf(MODEL_PATH);
    IF (model < 0) say("ERROR cargando " + MODEL_PATH); RETURN; END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = 5.0 / h;
    nsub = g3d_model_submesh_count(model);
    FOR (i = 0; i < nsub; i = i + 1)
        sm = g3d_model_submesh(model, i);
        st = g3d_model_submesh_texture(model, i);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        e = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(e, sm);
        g3d_entity_set_material(e, mat);
        g3d_entity_set_scale(e, sc, sc, sc);
        parts[i] = e;
    END

    // Barriles flotando en el lago
    barrel_model = g3d_load_gltf("assets/barrel.glb");
    make_floating_barrel(-15.0, -14.0);
    make_floating_barrel(-18.5, -11.0);
    make_floating_barrel(-12.5, -16.5);

    // Punto de inicio: si la escena tiene SPAWN (puesto en el editor), empezar ahi.
    IF (g3d_scene_has_spawn())
        px = g3d_scene_spawn_x(); pz = g3d_scene_spawn_z();
    END
    py = g3d_voxterrain_floor(px, 500.0, pz);
    IF (py < -99999.0) py = g3d_scene_terrain_height(px, pz); END

    say("WASD andar/nadar, raton girar camara. Cruza el lago para ver el chapoteo. ESC salir.");
    x = 480; y = 270;
    g3d_mouse_capture(1);

    // HUD
    write(0, 10, 10, 0, "FPS:");
    write_var(0, 70, 10, 0, frame_info.fps);
    write(0, 10, 28, 0, "en agua:");
    write_var(0, 90, 28, 0, inwater);
    write(0, 10, 46, 0, "WASD andar/nadar  raton camara  -  cruza el lago");
    write(0, 10, 64, 0, "1:HDR  2:Bloom  3:SSAO  4:Filmic  5:Reflejos");
    write_var(0, 300, 64, 0, f_hdr);
    write_var(0, 360, 64, 0, f_bloom);
    write_var(0, 420, 64, 0, f_ssao);
    write_var(0, 490, 64, 0, f_tone);
    write_var(0, 570, 64, 0, f_ssr);

    LOOP
        bobt = bobt + 4.0;     // avanzar el oleaje

        // Toggles de post-proceso (flanco de pulsacion)
        IF (key(_1) AND k1p == 0) f_hdr = 1 - f_hdr; g3d_set_hdr(f_hdr); END
        IF (key(_2) AND k2p == 0) f_bloom = 1 - f_bloom; g3d_set_bloom(f_bloom, 0.35, 0.9); END
        IF (key(_3) AND k3p == 0) f_ssao = 1 - f_ssao; g3d_set_ssao(f_ssao, 0.5, 1.2); END
        IF (key(_4) AND k4p == 0) f_tone = 1 - f_tone; g3d_set_tonemap(f_tone); END
        IF (key(_5) AND k5p == 0) f_ssr = 1 - f_ssr; g3d_water_set_ssr(f_ssr, 0.65); END
        k1p = key(_1); k2p = key(_2); k3p = key(_3); k4p = key(_4); k5p = key(_5);

        // Camara con el raton
        g3d_mouse_update();
        camyaw = camyaw + g3d_mouse_dx() * msens;

        // Direcciones en el suelo segun la camara
        fwx = sin(camyaw * 1000); fwz = cos(camyaw * 1000);
        rgx = cos(camyaw * 1000); rgz = -sin(camyaw * 1000);

        // Vector de movimiento
        mvx = 0.0; mvz = 0.0;
        IF (key(_W)) mvx = mvx + fwx; mvz = mvz + fwz; END
        IF (key(_S)) mvx = mvx - fwx; mvz = mvz - fwz; END
        IF (key(_D)) mvx = mvx + rgx; mvz = mvz + rgz; END
        IF (key(_A)) mvx = mvx - rgx; mvz = mvz - rgz; END

        moving = 0;
        mlen = sqrt(mvx*mvx + mvz*mvz);
        IF (mlen > 0.01)
            mvx = mvx / mlen; mvz = mvz / mlen;
            // Movimiento con colisión de cueva + subida de pendientes (motor).
            // Sube cuestas hasta 'climb', bloquea paredes/cliffs/techos bajos.
            g3d_voxterrain_walk(px, py, pz, prad, phei, pclimb, mvx*SPEED, mvz*SPEED);
            px = g3d_voxterrain_walkx();
            pz = g3d_voxterrain_walkz();
            py = g3d_voxterrain_walky();   // sube el escalón/pendiente
            // No salirse del mapa.
            IF (worldhalf > 1.0)
                IF (px >  worldhalf) px =  worldhalf; END
                IF (px < -worldhalf) px = -worldhalf; END
                IF (pz >  worldhalf) pz =  worldhalf; END
                IF (pz < -worldhalf) pz = -worldhalf; END
            END
            facing = atan2(mvx, mvz) / 1000.0 + FACE_OFF;   // grados
            moving = 1;
        END

        // Flotabilidad:
        //  - aguas profundas (no toca fondo): flota hundido y se mece
        //  - aguas poco profundas: vadea con los pies en el suelo
        //  - tierra seca: sobre el terreno
        // suelo: en terreno vóxel, el suelo de la cueva justo bajo los pies
        // (floor 3D); si no hay vóxel, la altura del heightmap.
        gy = g3d_voxterrain_floor(px, py + pclimb, pz);
        IF (gy < -99999.0) gy = g3d_scene_terrain_height(px, pz); END
        inwater = 0;
        floaty = waterlvl - SWIM_SUBMERGE;
        IF (waterlvl > -99999.0 AND gy < floaty)
            py = floaty + sin(bobt * 1000) * BOB_AMP;     // flota/nada
            inwater = 1;
        ELSE
            IF (waterlvl > -99999.0 AND gy < waterlvl)
                py = gy;                                  // vadea
                inwater = 1;
            ELSE
                py = gy;                                  // tierra seca
            END
        END

        // Chapoteo mientras se mueve dentro del agua
        IF (inwater AND moving)
            splashtimer = splashtimer - 1;
            IF (splashtimer <= 0)
                g3d_water_splash(px, py, pz, 1.0);
                splashtimer = 6;     // una salpicadura cada ~6 frames
            END
        ELSE
            splashtimer = 0;
        END

        // Animacion: andar o idle
        IF (moving) curanim = WALK_ANIM; ELSE curanim = IDLE_ANIM; END
        g3d_model_animate(model, curanim, atime, 1);
        atime = atime + 0.0166;

        // Colocar a Crash
        FOR (i = 0; i < nsub; i = i + 1)
            g3d_entity_set_position(parts[i], px, py, pz);
            g3d_entity_set_rotation(parts[i], 0.0, facing * 0.01745, 0.0);
        END

        // Mecer los barriles a flote
        FOR (i = 0; i < bn; i = i + 1)
            g3d_entity_set_position(bent[i], bbx[i],
                waterlvl - BARREL_SUBMERGE + sin((bobt + bbx[i]*8.0) * 1000) * BOB_AMP,
                bbz[i]);
        END

        // Camara 3a persona detras de Crash
        g3d_camera_set_position(camera, px - fwx*13.0, py + 7.0, pz - fwz*13.0);
        g3d_camera_look_at(camera, px, py + 3.0, pz,  0.0, 1.0, 0.0);

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    g3d_shutdown();
END
