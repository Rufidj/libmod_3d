// Tech demo: load two glTF (.glb) models (Mario + Crash) side by side, lit.
// Compile: bgdc TEST_GLTF.PRG   Run: bgdi TEST_GLTF.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int light;
    int ground_mesh;
    int ground_mat;
    int gid = 0;
    float ang = 0.0;
    // todas las submallas de ambos modelos
    int parts[64];
    int nparts = 0;
    int i;
END

// Carga un modelo glTF, le corrige la orientacion (pitch en radianes, horneado
// y re-apoyado en el suelo), lo escala a `target_h` de alto y lo coloca en (px,0,pz).
FUNCTION spawn_model(string path, float px, float pz, float target_h, float pitch)
PRIVATE
    int model;
    int n;
    int j;
    int sm;
    int st;
    int mat;
    int e;
    float h;
    float sc;
BEGIN
    model = g3d_load_gltf(path);
    IF (model < 0)
        say("ERROR cargando " + path);
        RETURN;
    END
    // Corregir orientacion (hornea en los vertices y re-apoya en el suelo)
    IF (pitch <> 0.0)
        g3d_model_orient(model, pitch, 0.0, 0.0);
    END
    h = g3d_model_height(model);   // altura DESPUES de orientar
    IF (h <= 0.0) h = 1.0; END
    sc = target_h / h;
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        e = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(e, sm);
        g3d_entity_set_material(e, mat);
        g3d_entity_set_scale(e, sc, sc, sc);
        g3d_entity_set_position(e, px, 0.0, pz);
        parts[nparts] = e;
        nparts = nparts + 1;
    END
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    IF (!g3d_init(960, 540))
        say("ERROR: 3D init failed");
        RETURN;
    END

    scene = g3d_scene_create("glTF");
    g3d_scene_set_active(scene);

    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 4.5, -18.0);
    g3d_camera_look_at(camera, 0.0, 2.5, 0.0,  0.0, 1.0, 0.0);
    g3d_camera_set_active(camera);

    // Suelo
    ground_mesh = g3d_primitive_plane();
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 0.5, 0.55, 0.6, 1.0);
    i = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(i, ground_mesh);
    g3d_entity_set_material(i, ground_mat);
    g3d_entity_set_scale(i, 40.0, 1.0, 40.0);

    // Mario + Luigi (Luigi venia haciendo el pino -> 180 sobre X)
    spawn_model("assets/mario.glb", -4.0, 0.0, 5.0,  0.0);
    spawn_model("assets/luigi.glb",  4.0, 0.0, 5.0,  3.14159);
    say("Submallas totales: " + itoa(nparts));

    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_set_shadows(1);
    g3d_set_sun(0.22);

    x = 480;
    y = 270;

    LOOP
        ang = ang + 0.01;
        FOR (i = 0; i < nparts; i = i + 1)
            g3d_entity_set_rotation(parts[i], 0.0, ang, 0.0);
        END

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_shutdown();
    say("=== Done ===");
END
