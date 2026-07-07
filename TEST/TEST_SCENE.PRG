// Escena: hierba (grass), montañas escarpadas (terrain) en los bordes, un lago
// excavado (water) con cascada, y Mario + Luigi. Camara libre WASD/flechas/Q-E.
// Compile: bgdc TEST_SCENE.PRG   Run: bgdi TEST_SCENE.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int light;
    int grass_tex;
    int terr_tex;
    int water_tex;
    int ground_mesh;
    int ground_mat;
    int mtn_mat;
    int e;
    int i;
    int spray;
    int splash;
    int sky_tex;
    int spin[64];
    int spin_n = 0;
    float spin_yaw = 0.0;
    int gid = 0;
    float cx = 0.0;
    float cy = 16.0;
    float cz = -56.0;
    float yaw = 0.0;
    float pitch = -10.0;
    float fwx; float fwy; float fwz;
    float ms = 0.7;
    float ts = 2.0;
END

// Una montaña escarpada propia (semilla unica) en (px,pz), escala sc, giro yaw
FUNCTION place_mountain(float px, float pz, float sc, int seed, float yawr)
PRIVATE
    int mesh;
    int m;
BEGIN
    mesh = g3d_primitive_mountain(44, 46.0, 36.0, 12.0, seed, 0.0);
    m = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(m, mesh);
    g3d_entity_set_material(m, mtn_mat);
    g3d_entity_set_scale(m, sc, sc, sc);
    g3d_entity_set_position(m, px, -1.0, pz);
    g3d_entity_set_rotation(m, 0.0, yawr, 0.0);
END

// Carga un modelo glTF, lo orienta (pitch), escala a target_h y lo planta en (px,pz)
// sobre el terreno.
FUNCTION place_model(string path, float px, float pz, float target_h, float pitch_)
PRIVATE
    int model; int n; int j; int sm; int st; int mat; int ent; float h; float sc; float gy;
BEGIN
    model = g3d_load_gltf(path);
    IF (model < 0) RETURN; END
    IF (pitch_ <> 0.0) g3d_model_orient(model, pitch_, 0.0, 0.0); END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = target_h / h;
    gy = g3d_terrain_get_height(ground_mesh, px, pz);
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, sc, sc, sc);
        g3d_entity_set_position(ent, px, gy, pz);
        // registrar para girar lentamente
        spin[spin_n] = ent;
        spin_n = spin_n + 1;
    END
    
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    scene = g3d_scene_create("Escena");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    grass_tex = g3d_load_texture("assets/grass.png");
    terr_tex  = g3d_load_texture("assets/terrain.png");
    water_tex = g3d_load_texture("assets/water.png");

    // Terreno de hierba
    ground_mesh = g3d_primitive_terrain(110, 110.0, 2.5, 26.0, 5);
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_roughness(ground_mat, 0.9);
    IF (grass_tex >= 0) g3d_material_set_texture(ground_mat, 0, grass_tex); END
    e = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(e, ground_mesh);
    g3d_entity_set_material(e, ground_mat);

    // Excavar un hoyo central para el lago (varias pasadas para suavizar bordes)
    g3d_terrain_raise(ground_mesh,  0.0, 6.0, 22.0, -7.0);
    g3d_terrain_raise(ground_mesh,  0.0, 6.0, 14.0, -4.0);
    g3d_terrain_smooth(ground_mesh, 0.0, 6.0, 26.0,  0.8);

    // Material de montaña (terrain.png), compartido
    mtn_mat = g3d_material_create();
    g3d_material_set_color(mtn_mat, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_roughness(mtn_mat, 0.95);
    IF (terr_tex >= 0) g3d_material_set_texture(mtn_mat, 0, terr_tex); END

    // Anillo de montañas escarpadas (cada una semilla y giro distintos)
    place_mountain(-44.0,  14.0, 1.4, 11, 0.6);
    place_mountain(-26.0,  40.0, 1.7, 23, 2.1);
    place_mountain(  4.0,  48.0, 1.9, 37, 4.0);
    place_mountain( 30.0,  40.0, 1.6, 51, 1.2);
    place_mountain( 46.0,  14.0, 1.4, 67, 5.3);
    place_mountain(-48.0, -24.0, 1.2, 83, 3.3);
    place_mountain( 48.0, -26.0, 1.2, 95, 0.9);

    // Lago (water.png) que llena el hoyo. Es un lago -> agua muy calmada.
    g3d_water_create(0.2, 130.0, 48);
    g3d_water_set_waves(0.06, 4.0, 0.5);
    g3d_water_set_color(0.02, 0.18, 0.30,  0.15, 0.42, 0.55);
    IF (water_tex >= 0) g3d_water_set_texture(water_tex); END
    // Reflejos planares: montañas/Mario/Luigi reflejados en el lago
    g3d_water_set_reflection(1, 0.7);

    // Cascada cayendo al lago + spray
    IF (water_tex >= 0) g3d_flow_set_texture(water_tex); END
    g3d_flow_set_color(0.12, 0.34, 0.46);
    g3d_flow_add(4.0, 20.0, 40.0,   4.0, 0.2, 22.0,   6.0, 1.6, 5.0);
    g3d_flow_set_clip(0.0);
    spray = g3d_particles_create(4.0, 19.0, 39.0,  2.2, 1.0, 1.5,  0.0, -4.0, -7.0, 1.4, 9.0, 0.16, 2.6, 1500);
    g3d_particles_set_color(spray, 0.92, 0.96, 1.0);
    g3d_particles_set_floor(spray, 0.1);
    splash = g3d_particles_create(4.0, 0.4, 22.0,  3.0, 0.2, 3.0,  0.0, 5.0, 0.0, 3.5, 12.0, 0.15, 1.0, 800);
    g3d_particles_set_color(splash, 1.0, 1.0, 1.0);
    g3d_particles_set_floor(splash, -0.5);

    // Mario y Luigi a la orilla del lago
    place_model("assets/mario.glb", -10.0, -10.0, 4.0,  0.0);
    place_model("assets/luigi.glb",  10.0, -10.0, 4.0,  0.0);

    // Sol + sombras
    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_set_shadows(1);
    g3d_set_sun(0.20);

    // Cielo: panorama de nubes (assets/Skybox.jpg)
    sky_tex = g3d_load_texture("assets/Skybox.jpg");
    IF (sky_tex >= 0)
        g3d_sky_set_texture(sky_tex);
    ELSE
        g3d_sky_set_gradient(0.22, 0.45, 0.85,  0.82, 0.89, 0.96);
    END
    // Niebla clara para fundir el fondo con las montañas
    g3d_set_fog(1,  0.82, 0.89, 0.96,  55.0, 150.0);

    say("WASD/flechas/Q-E para moverte. ESC para salir.");
    x = 480;
    y = 270;

    LOOP
     
        IF (key(_LEFT))  yaw = yaw - ts; END
        IF (key(_RIGHT)) yaw = yaw + ts; END
        IF (key(_UP))    pitch = pitch + ts; END
        IF (key(_DOWN))  pitch = pitch - ts; END
        IF (pitch > 85.0)  pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END

        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);

        IF (key(_W)) cx = cx + fwx*ms; cy = cy + fwy*ms; cz = cz + fwz*ms; END
        IF (key(_S)) cx = cx - fwx*ms; cy = cy - fwy*ms; cz = cz - fwz*ms; END
        IF (key(_A)) cx = cx - cos(yaw*1000)*ms; cz = cz + sin(yaw*1000)*ms; END
        IF (key(_D)) cx = cx + cos(yaw*1000)*ms; cz = cz - sin(yaw*1000)*ms; END
        IF (key(_Q)) cy = cy - ms; END
        IF (key(_E)) cy = cy + ms; END

        g3d_camera_set_position(camera, cx, cy, cz);
        g3d_camera_look_at(camera, cx+fwx, cy+fwy, cz+fwz,  0.0, 1.0, 0.0);

        // Mario y Luigi giran lentamente sobre si mismos
        spin_yaw = spin_yaw + 0.01;
        FOR (i = 0; i < spin_n; i = i + 1)
            g3d_entity_set_rotation(spin[i], 0.0, spin_yaw, 0.0);
        END

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
        write(0, 10, 10, 0, "FPS: " + frame_info.fps); 
    END

    g3d_shutdown();
END
