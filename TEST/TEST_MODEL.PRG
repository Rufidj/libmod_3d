// Inspector de modelos: carga uno, lo muestra grande de frente, y lo rotas con
// las flechas para encontrar su orientacion correcta (la consola te da el angulo
// en radianes para pasarselo a g3d_model_orient). PgUp/PgDn gira en yaw.
// Cambia MODEL_PATH por el modelo que quieras probar.
// Compile: bgdc TEST_MODEL.PRG   Run: bgdi TEST_MODEL.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    MODEL_PATH = "assets/mario.glb";
END

GLOBAL
    int scene; int camera; int light;
    int model; int nsub; int i;
    int parts[64];
    int gid = 0;
    float pitch = 0.0;   // grados
    float yaw = 0.0;
    int prev = 0;
END

PROCESS main()
PRIVATE
    int j; int sm; int st; int mat; int e; float h; float sc;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    scene = g3d_scene_create("Inspector");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 3.0, -9.0);
    g3d_camera_look_at(camera, 0.0, 3.0, 0.0,  0.0, 1.0, 0.0);
    g3d_camera_set_active(camera);

    model = g3d_load_gltf(MODEL_PATH);
    IF (model < 0)
        say("ERROR cargando " + MODEL_PATH);
        RETURN;
    END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = 5.0 / h;
    nsub = g3d_model_submesh_count(model);
    say("Modelo: " + MODEL_PATH + "  submallas=" + itoa(nsub));

    FOR (i = 0; i < nsub; i = i + 1)
        sm = g3d_model_submesh(model, i);
        st = g3d_model_submesh_texture(model, i);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0)
            g3d_material_set_texture(mat, 0, st);
        ELSE
            say("  submalla " + itoa(i) + " SIN textura");
        END
        e = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(e, sm);
        g3d_entity_set_material(e, mat);
        g3d_entity_set_scale(e, sc, sc, sc);
        parts[i] = e;
    END

    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_light_set_direction(light, -0.4, -0.7, -0.5);
    g3d_light_set_intensity(light, 1.4);
    g3d_set_ambient_light(0.5, 0.5, 0.55, 1.0);
    g3d_set_clear_color(0.15, 0.16, 0.2, 1.0);

    say("Flechas ARRIBA/ABAJO = pitch, IZQ/DCHA = yaw. Lee el angulo en consola.");
    x = 480;
    y = 270;

    LOOP
        IF (key(_UP))    pitch = pitch + 2.0; END
        IF (key(_DOWN))  pitch = pitch - 2.0; END
        IF (key(_LEFT))  yaw = yaw - 2.0; END
        IF (key(_RIGHT)) yaw = yaw + 2.0; END

        // rotar todas las submallas (pitch/yaw en radianes)
        FOR (i = 0; i < nsub; i = i + 1)
            g3d_entity_set_rotation(parts[i], pitch * 0.01745, yaw * 0.01745, 0.0);
        END

        // imprimir el angulo cada ~20 frames
        prev = prev + 1;
        IF (prev >= 20)
            prev = 0;
            say("pitch(rad)=" + ftoa(pitch * 0.01745) + "  yaw(rad)=" + ftoa(yaw * 0.01745));
        END

        gid = g3d_render(gid);
        graph = gid;
        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_shutdown();
END
