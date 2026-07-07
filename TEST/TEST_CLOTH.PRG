// Simulacion de telas con PERSONAJE jugable: Crash camina (WASD) y salta (SPACE);
// al cruzar las sabanas colgadas estas se APARTAN a su paso. Tambien hay una
// bandera que ondea con el viento. Camara 3a persona, raton = girar.
// Animaciones (segun el modelo): 3=idle, 2=andar, 1=saltar.
// Compile: bgdc TEST_CLOTH.PRG   Run: bgdi TEST_CLOTH.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    MODEL_PATH = "assets/crash.glb";
    IDLE_ANIM = 3;
    JUMP_ANIM = 1;
    WALK_ANIM = 2;
    SPEED   = 0.40;
    GRAV    = 0.030;
    JUMPVEL = 0.62;
END

GLOBAL
    int scene; int camera; int light;
    int grass_tex; int cloth_tex; int flag_tex;
    int ground_mesh; int ground_mat;
    int model; int nsub; int i;
    int parts[64];
    int sheet[8]; int nsheets = 0; int flag;
    int gid = 0;

    // estado del personaje
    float px = 0.0; float py = 0.0; float pz = -16.0;
    float vy = 0.0;
    int grounded = 1;
    float facing = 0.0;
    float camyaw = 0.0;
    float msens = 0.12;

    // animacion
    int curanim = 0;
    float atime = 0.0;
    int prevjump = 0;
    int nanim = 0;

    float fwx; float fwz; float rgx; float rgz;
    float mvx; float mvz; float mlen;
    float gy; float sc; float h;
    int moving;
END

// Cuelga una sabana (borde superior fijado) centrada en (cx,cz), de anchura w,
// alto hgt, con su cima a la altura ytop. Guarda el id y le pone viento+textura.
FUNCTION add_sheet(float cx, float ytop, float cz, float w, float hgt)
PRIVATE
    int s;
BEGIN
    s = g3d_cloth_create(w, hgt, 18, 18,  cx - w * 0.5, ytop, cz);
    g3d_cloth_pin(s, 0);
    g3d_cloth_set_wind(s, 0.3, 0.0, 0.15, 1.5);
    IF (cloth_tex >= 0) g3d_cloth_set_texture(s, cloth_tex); END
    sheet[nsheets] = s;
    nsheets = nsheets + 1;
END

PROCESS main()
PRIVATE
    int j; int sm; int st; int mat; int e;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    scene = g3d_scene_create("Telas");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Terreno plano (para que las sabanas cuelguen limpio hasta el suelo)
    grass_tex = g3d_load_texture("assets/grass.png");
    ground_mesh = g3d_primitive_terrain(100, 120.0, 0.0, 1.0, 1);
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 1.0, 1.0, 1.0, 1.0);
    IF (grass_tex >= 0) g3d_material_set_texture(ground_mat, 0, grass_tex); END
    e = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(e, ground_mesh);
    g3d_entity_set_material(e, ground_mat);

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

    // Luz + cielo + sombras
    light = g3d_light_create(0, 1.0, 1.0, 0.95);
    g3d_light_set_direction(light, -0.4, -0.8, -0.35);
    g3d_light_set_intensity(light, 1.3);
    g3d_set_ambient_light(0.45, 0.47, 0.5, 1.0);
    g3d_sky_set_gradient(0.30, 0.50, 0.85,  0.80, 0.88, 0.96);
    g3d_set_shadows(1);

    cloth_tex = g3d_load_texture("assets/terrain.png");
    flag_tex  = g3d_load_texture("assets/water.png");

    // Pasillo de sabanas colgadas que Crash atraviesa (avanza con W hacia +Z)
    add_sheet(0.0, 7.5, -6.0,  9.0, 8.0);
    add_sheet(0.0, 7.5,  0.0,  9.0, 8.0);
    add_sheet(0.0, 7.5,  6.0,  9.0, 8.0);
    add_sheet(0.0, 7.5, 12.0,  9.0, 8.0);

    // Bandera ondeando a un lado (borde izquierdo al mastil)
    flag = g3d_cloth_create(7.0, 4.5, 16, 11,  12.0, 12.0, 2.0);
    g3d_cloth_pin(flag, 2);
    g3d_cloth_set_wind(flag, 1.0, 0.0, 0.2, 9.0);
    IF (flag_tex >= 0) g3d_cloth_set_texture(flag, flag_tex); END

    py = g3d_terrain_get_height(ground_mesh, px, pz);
    nanim = g3d_model_anim_count(model);

    say("WASD andar, SPACE saltar, raton camara. Cruza las sabanas con W.");
    x = 480; y = 270;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "FPS:");
    write_var(0, 70, 10, 0, frame_info.fps);
    write(0, 10, 28, 0, "WASD andar  SPACE saltar  -  cruza las sabanas");

    LOOP
        // Camara con el raton
        g3d_mouse_update();
        camyaw = camyaw + g3d_mouse_dx() * msens;

        fwx = sin(camyaw * 1000); fwz = cos(camyaw * 1000);
        rgx = cos(camyaw * 1000); rgz = -sin(camyaw * 1000);

        mvx = 0.0; mvz = 0.0;
        IF (key(_W)) mvx = mvx + fwx; mvz = mvz + fwz; END
        IF (key(_S)) mvx = mvx - fwx; mvz = mvz - fwz; END
        IF (key(_D)) mvx = mvx + rgx; mvz = mvz + rgz; END
        IF (key(_A)) mvx = mvx - rgx; mvz = mvz - rgz; END

        moving = 0;
        mlen = sqrt(mvx*mvx + mvz*mvz);
        IF (mlen > 0.01)
            mvx = mvx / mlen; mvz = mvz / mlen;
            px = px + mvx * SPEED;
            pz = pz + mvz * SPEED;
            facing = atan2(mvx, mvz) / 1000.0;
            moving = 1;
        END

        // Gravedad / salto sobre terreno plano
        gy = g3d_terrain_get_height(ground_mesh, px, pz);
        IF (grounded AND key(_SPACE)) vy = JUMPVEL; grounded = 0; END
        IF (grounded == 0)
            vy = vy - GRAV;
            py = py + vy;
            IF (py <= gy) py = gy; vy = 0.0; grounded = 1; END
        ELSE
            py = gy;
        END

        // La tela reacciona al personaje: esfera-colisionador a la altura del torso
        FOR (i = 0; i < nsheets; i = i + 1)
            g3d_cloth_set_collider(sheet[i], px, py + 2.5, pz, 2.6);
            g3d_cloth_update(sheet[i], 0.016);
        END
        g3d_cloth_update(flag, 0.016);

        // Animacion
        IF (grounded == 0)
            IF (prevjump == 0) atime = 0.0; END
            curanim = JUMP_ANIM; prevjump = 1;
            g3d_model_animate(model, curanim, atime, 0);
        ELSE
            prevjump = 0;
            IF (moving) curanim = WALK_ANIM; ELSE curanim = IDLE_ANIM; END
            g3d_model_animate(model, curanim, atime, 1);
        END
        atime = atime + 0.0166;

        // Colocar a Crash
        FOR (i = 0; i < nsub; i = i + 1)
            g3d_entity_set_position(parts[i], px, py, pz);
            g3d_entity_set_rotation(parts[i], 0.0, facing * 1000, 0.0);
        END

        // Camara 3a persona
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
