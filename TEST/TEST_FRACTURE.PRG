// ============================================================================
//  TEST_FRACTURE.PRG - Fractura real con Jolt: una caja de madera INTACTA que,
//  al recibir un cocazo, ESTALLA en sus trozos reales (pre-fracturados en
//  Blender con Cell Fracture). Cada trozo es un cuerpo rigido Jolt.
//  WASD+raton = camara,  ESPACIO = lanzar coco,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int mbox;       // caja intacta (wood_box.glb)
    int mchunks;    // caja fracturada (crash_wood_box.glb, 10 trozos)
    int mcoco;
    int quit;
    float chunkh;   // alto del modelo de trozos ensamblado (para escalar)
    // === vida util (en FRAMES, a 60fps) antes de que desaparezcan. Subelos si
    //     quieres que los restos duren mas en escena (0 = no desaparecen). ===
    int chunklife;  // trozos de caja rota (0 = sin limite de tiempo)
    int cocolife;   // cocos lanzados
    // === pool acotado de escombros (ring buffer): coste constante ===
    // Como mucho NDEBRIS trozos vivos a la vez; al crear el trozo N+1 se retira
    // el mas antiguo. Restos "para siempre" en escena sin acumular cuerpos.
    int NDEBRIS;            // capacidad (se fija en main, <= 500)
    int dbr_owner[499];     // 500 slots: id del proceso trozo que ocupa cada uno
    int dbr_head;           // siguiente slot a escribir
    // registro de barriles vivos: cada caja rompible mira aqui para saber si
    // un barril la ha golpeado (y con que direccion).
    int   b_used[15];
    float b_x[15]; float b_y[15]; float b_z[15];
    float b_vx[15]; float b_vy[15]; float b_vz[15];
END

// Crea una entidad con UN submesh (un trozo) + su material.
FUNCTION int spawn_submesh_entity(int model, int idx)
PRIVATE
    int sm; int st; int mat; int ent; int sn; int smt; int sr;
BEGIN
    sm  = g3d_model_submesh(model, idx);
    st  = g3d_model_submesh_texture(model, idx);
    mat = g3d_material_create();
    g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
    IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
    sn  = g3d_model_submesh_map(model, idx, 1);
    smt = g3d_model_submesh_map(model, idx, 2);
    sr  = g3d_model_submesh_map(model, idx, 3);
    IF (sn  >= 0) g3d_material_set_map(mat, 1, sn);  END
    IF (smt >= 0) g3d_material_set_map(mat, 2, smt); END
    IF (sr  >= 0) g3d_material_set_map(mat, 3, sr);  END
    ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(ent, sm);
    g3d_entity_set_material(ent, mat);
    RETURN ent;
END

// Un trozo de caja: cuerpo Jolt dimensionado desde su submesh. Recibe la
// direccion del golpe (hvx,hvy,hvz) para el estallido. Se autodestruye tras
// unos segundos para no acumular cientos de cuerpos.
PROCESS chunk(int model, int idx, float wx, float wy, float wz, float s,
              float hvx, float hvy, float hvz)
PRIVATE
    int body; int ent; int life; int myslot;
    float cx; float cy; float cz;
    float ex; float ey; float ez; float el; float hl; float vx; float vy; float vz;
BEGIN
    // centroide del trozo (para el estallido radial); el colisionador convex se
    // construye internamente alrededor de este centroide.
    cx = g3d_model_submesh_cx(model, idx) * s;
    cy = g3d_model_submesh_cy(model, idx) * s;
    cz = g3d_model_submesh_cz(model, idx) * s;

    ent  = spawn_submesh_entity(model, idx);
    g3d_entity_set_scale(ent, s, s, s);
    // trozo = CONVEX HULL con su forma real (no un cubo). (wx,wy,wz) = origen del
    // modelo en el mundo; el creador coloca el cuerpo y el offset automaticamente.
    body = g3d_rigidbody_create_convex(wx, wy, wz, model, idx, s, 1.5);
    IF (body < 0)   // trozo degenerado: caja de respaldo desde su AABB
        body = g3d_rigidbody_create(wx + cx, wy + cy, wz + cz,
                                    g3d_model_submesh_hx(model, idx) * s * 0.5,
                                    g3d_model_submesh_hy(model, idx) * s * 0.5,
                                    g3d_model_submesh_hz(model, idx) * s * 0.5, 1.5);
        g3d_rigidbody_set_model_offset(body, cx, cy, cz);
    END
    // estallido: burst radial + empuje en la direccion del barrilazo + arriba
    ex = cx; ey = cy; ez = cz; el = sqrt(ex*ex + ey*ey + ez*ez);
    IF (el < 0.01) ex = 0.0; ey = 1.0; ez = 0.0; el = 1.0; END
    hl = sqrt(hvx*hvx + hvy*hvy + hvz*hvz); IF (hl < 0.01) hl = 1.0; END
    vx = ex/el * 3.5 + hvx/hl * 2.5;
    vy = ey/el * 3.5 + hvy/hl * 2.5 + 3.5;
    vz = ez/el * 3.5 + hvz/hl * 2.5;
    g3d_rigidbody_set_velocity(body, vx, vy, vz);
    // giro suave (±~2 rad/s): voltean en el aire sin volverse locos
    g3d_rigidbody_set_angular_velocity(body, (rand(0,100) - 50) / 25.0,
                                             (rand(0,100) - 50) / 25.0,
                                             (rand(0,100) - 50) / 25.0);
    g3d_rigidbody_set_bounce(body, 0.1, 0.5);   // friccion moderada: no se pegan, pero asientan

    // reclama un slot del pool acotado; el trozo que lo ocupaba (si habia) ve
    // que su slot cambio de dueno y se retira solo.
    myslot = dbr_head;
    dbr_head = dbr_head + 1; IF (dbr_head >= NDEBRIS) dbr_head = 0; END
    dbr_owner[myslot] = id;

    life = 0;
    LOOP
        g3d_entity_set_position(ent, g3d_rigidbody_render_x(body),
                                     g3d_rigidbody_render_y(body),
                                     g3d_rigidbody_render_z(body));
        g3d_entity_set_rotation(ent, g3d_rigidbody_angle_x(body),
                                     g3d_rigidbody_angle_y(body),
                                     g3d_rigidbody_angle_z(body));
        IF (dbr_owner[myslot] != id) BREAK; END   // otro trozo reclamo mi slot
        life = life + 1;
        IF (chunklife > 0 AND life > chunklife) BREAK; END   // limite de tiempo opcional
        IF (quit) BREAK; END
        FRAME;
    END
    IF (dbr_owner[myslot] == id) dbr_owner[myslot] = 0; END
    g3d_rigidbody_destroy(body);
    g3d_model_despawn(ent);
END

// Caja rompible y apilable: es un cuerpo rigido normal (cae, se apila, la
// empujan) HASTA que un barril la golpea; entonces se oculta y estalla en sus
// trozos reales. s = alto/ancho de la caja.
PROCESS breakbox(float px, float py, float pz, float s)
PRIVATE
    int body; int mdl; int i; int n; int hit; int k;
    float h; float cscale; float bx; float by; float bz;
    float dx; float dy; float dz; float hvx; float hvy; float hvz;
BEGIN
    h      = s * 0.5;
    cscale = s / chunkh;                 // escala de los trozos = tamano de ESTA caja
    mdl    = g3d_model_spawn(scene, mbox, px, py, pz, s, 0.0);
    body   = g3d_rigidbody_create(px, py + h, pz, h, h, h, 2.0);
    g3d_rigidbody_set_bounce(body, 0.0, 0.7);   // se apila estable

    hit = 0;
    LOOP
        bx = g3d_rigidbody_x(body); by = g3d_rigidbody_y(body); bz = g3d_rigidbody_z(body);
        g3d_entity_set_position(mdl, g3d_rigidbody_render_x(body),
                                     g3d_rigidbody_render_y(body),
                                     g3d_rigidbody_render_z(body));
        g3d_entity_set_rotation(mdl, g3d_rigidbody_angle_x(body),
                                     g3d_rigidbody_angle_y(body),
                                     g3d_rigidbody_angle_z(body));
        // busca un barril vivo dentro del radio de impacto
        FOR (k = 0; k <= 15; k = k + 1)
            IF (b_used[k])
                dx = b_x[k] - bx; dy = b_y[k] - by; dz = b_z[k] - bz;
                IF (dx*dx + dy*dy + dz*dz < (h + 0.7) * (h + 0.7))
                    hvx = b_vx[k]; hvy = b_vy[k]; hvz = b_vz[k];
                    hit = 1;
                END
            END
        END
        IF (hit) BREAK; END
        IF (quit) BREAK; END
        FRAME;
    END

    g3d_rigidbody_destroy(body);
    g3d_model_despawn(mdl);

    IF (hit AND quit == 0)
        // estalla en trozos en su posicion/tamano actuales
        n = g3d_model_submesh_count(mchunks);
        FOR (i = 0; i < n; i = i + 1)
            chunk(mchunks, i, bx, by - h, bz, cscale, hvx, hvy, hvz);
        END
    END
END

// Coco lanzado: cuerpo rigido que se registra globalmente para que las
// cajas rompibles detecten el impacto. Pequeno y compacto pero pesado, para
// que reviente la caja al impactar.
PROCESS coco(float px, float py, float pz, float ivx, float ivy, float ivz)
PRIVATE
    int body; int mdl; int slot; int life;
BEGIN
    mdl  = g3d_model_spawn(scene, mcoco, px, py, pz, 0.9, 0.0);   // ~0.9u de alto
    // coco = ESFERA (rueda de verdad) + CCD (no atraviesa cajas al ir rapido)
    body = g3d_rigidbody_create_sphere(px, py, pz, 0.45, 6.0);
    g3d_rigidbody_set_ccd(body, 1);
    g3d_rigidbody_set_velocity(body, ivx, ivy, ivz);
    g3d_rigidbody_set_bounce(body, 0.25, 0.4);   // un coco rebota un poco

    // reserva un hueco en el registro de barriles
    slot = -1;
    FOR (slot = 0; slot <= 15; slot = slot + 1)
        IF (b_used[slot] == 0) BREAK; END
    END
    IF (slot <= 15)
        b_used[slot] = 1;
        b_vx[slot] = ivx; b_vy[slot] = ivy; b_vz[slot] = ivz;
    END

    life = 0;
    LOOP
        g3d_entity_set_position(mdl, g3d_rigidbody_render_x(body),
                                     g3d_rigidbody_render_y(body),
                                     g3d_rigidbody_render_z(body));
        g3d_entity_set_rotation(mdl, g3d_rigidbody_angle_x(body),
                                     g3d_rigidbody_angle_y(body),
                                     g3d_rigidbody_angle_z(body));
        IF (slot <= 15)
            b_x[slot] = g3d_rigidbody_x(body);
            b_y[slot] = g3d_rigidbody_y(body);
            b_z[slot] = g3d_rigidbody_z(body);
        END
        life = life + 1;
        IF (cocolife > 0 AND life > cocolife) BREAK; END   // el coco desaparece
        IF (quit) BREAK; END
        FRAME;
    END
    IF (slot <= 15) b_used[slot] = 0; END
    g3d_rigidbody_destroy(body);
    g3d_model_despawn(mdl);
END

PROCESS main()
PRIVATE
    int light; int i; int ksp;
    float dt; float msens;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz; float spd;
    float sx; float sz; float gy; float ty; float tz; float bz; float bg;
    int col; int row; float wallx; float wallz; float wallg; float cs;
BEGIN
    set_mode(1920, 1080);
    set_fps(0, 0);

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.93, 0.82);
    g3d_light_set_direction(light, -0.5, -0.7, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.30, 0.34, 0.42, 1.0);

    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.75, 0.80, 0.85);
    g3d_sky_set_clouds(0.5, 1.0);
    g3d_sky_enable(1);

    g3d_set_hdr(1);
    g3d_set_tonemap(1);
    g3d_set_exposure(1.0);
    g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);

    mbox    = g3d_load_gltf("assets/wood_box.glb");
    mchunks = g3d_load_gltf_fractured("assets/crash_wood_box.glb");
    mcoco   = g3d_load_gltf("assets/coconut.glb");
    chunkh = g3d_model_height(mchunks);       // alto del modelo de trozos
    IF (chunkh <= 0.0) chunkh = 1.0; END

    // Pool acotado de escombros: hasta NDEBRIS trozos vivos a la vez. Los restos
    // se quedan en escena indefinidamente y solo desaparecen cuando revientas
    // tantas cajas que hacen falta sus slots. Coste de fisica/render ACOTADO.
    NDEBRIS   = 300;        // <= 500 (tamano del array dbr_owner)
    dbr_head  = 0;
    chunklife = 0;          // 0 = sin limite de tiempo (manda el pool). Pon p.ej.
                            // 20*60 si ademas quieres que caduquen a los 20s.
    cocolife  = 12 * 60;    // cocos ~12s

    // tierra firme
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz = 110.0 - i * 3.0;
        ty = g3d_scene_terrain_height(0.0, tz);
        IF (ty > 4.0) sz = tz; gy = ty; BREAK; END
    END

    g3d_physics_set_gravity(24.0);

    quit = 0;

    // caja suelta delante del punto de aparicion (cerca = terreno mas plano)
    bz = sz - 8.0; bg = g3d_scene_terrain_height(sx, bz);
    breakbox(sx, bg, bz, 2.0);

    // Pared de cajas ROMPIBLES apilada al lado: 5 columnas x 5 filas. Cada una
    // se apila con Jolt y ESTALLA en trozos al recibir un barril. Hueco minimo
    // para que no spawneen solapadas (evita el "salto" inicial de penetracion).
    cs = 1.8;
    wallx = sx + 9.0;
    wallz = bz;
    wallg = g3d_scene_terrain_height(wallx, wallz);
    FOR (col = 0; col < 5; col = col + 1)
        FOR (row = 0; row < 5; row = row + 1)
            breakbox(wallx + (col - 2) * (cs + 0.04),
                     wallg + row * (cs + 0.02),
                     wallz, cs);
        END
    END

    camx = sx; camy = gy + 3.0; camz = sz + 4.0;
    yaw = 180.0; pitch = -4.0; msens = 0.18; dt = 1.0 / 60.0; spd = 1.0;

    x = 480; y = 270;
    write(0, 10, 10, 0, "WASD+raton - ESPACIO lanzar COCO - las cajas ESTALLAN en trozos - ESC salir");
    write(0, 820, 10, 0, "FPS:"); write_var(0, 860, 10, 0, frame_info.fps);
    g3d_mouse_capture(1);

    LOOP
        g3d_rigidbody_step(dt);

        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END
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

        IF (key(_SPACE) AND ksp == 0)
            coco(camx + fwx * 3.0, camy + fwy * 3.0, camz + fwz * 3.0,
                 fwx * 20.0, fwy * 20.0 + 1.0, fwz * 20.0);
        END
        ksp = key(_SPACE);

        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
END
