// Mini-editor de mundo: ESCULPIR terreno, PINTAR texturas y COLOCAR prefabs,
// todo con el RATON en tiempo real (rayo de pantalla -> punto del terreno).
//   Herramientas (teclas):  1 pilar  2 habitacion  3 subir  4 bajar  5 suavizar
//                           6 pintar roca  7 pintar hierba
//   RATON apuntar, CLICK IZQ aplicar/colocar.  + / -  radio del pincel.
//   Mover camara: WASD, flechas mirar, Q/E subir/bajar.  ESC salir.
// Compile: bgdc TEST_EDITOR.PRG   Run: bgdi TEST_EDITOR.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light;
    int grass_tex; int rock_tex; int ground; int ground_mat;
    int canvas; int canvas_tex;
    int pillar; int room;
    int ghost_mesh; int ghost_mat; int ghost;
    int tool = 0;            // 0 pilar 1 room 2 raise 3 lower 4 smooth 5 rock 6 grass
    float brush = 8.0;
    int placed = 0; int prevclick = 0; int e;
    
    float cx = 0.0; float cy = 22.0; float cz = -45.0;
    float yaw = 0.0; float pitch = -24.0;
    float fwx; float fwy; float fwz;
    float ms = 0.9; float ts = 2.0;
    int hit; float hx; float hz;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    

    scene = g3d_scene_create("Editor");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    grass_tex = g3d_load_texture("assets/grass.png");
    rock_tex  = g3d_load_texture("assets/terrain.png");

    // Terreno con tiling=1 para que el lienzo de pintura mapee 1:1
    ground = g3d_primitive_terrain(140, 200.0, 4.0, 1.0, 5);
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 1.0, 1.0, 1.0, 1.0);

    // Lienzo de pintura: base hierba (tileada), usado como albedo del terreno
    canvas = g3d_paint_create(1024, 1024);
    IF (grass_tex >= 0) g3d_paint_fill(canvas, grass_tex, 40.0); END
    canvas_tex = g3d_paint_get_texture(canvas);
    g3d_material_set_texture(ground_mat, 0, canvas_tex);

    e = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(e, ground);
    g3d_entity_set_material(e, ground_mat);

    g3d_sky_set_gradient(0.30, 0.50, 0.85,  0.80, 0.88, 0.96);
    light = g3d_light_create(0, 1.0, 1.0, 0.95);
    g3d_light_set_direction(light, -0.4, -0.8, -0.35);
    g3d_light_set_intensity(light, 1.2);
    g3d_set_ambient_light(0.5, 0.52, 0.55, 1.0);
    g3d_set_shadows(1);

    pillar = g3d_prefab_create("pilar");
    g3d_prefab_add_box(pillar, 0.0, 3.0, 0.0,  2.0, 6.0, 2.0,  0.0,0.0,0.0,  0.8,0.7,0.55, 1);
    g3d_prefab_piece_texture(pillar, "assets/terrain.png");
    room = g3d_prefab_load("room.prefab");

    // Cubo "fantasma" que marca el pincel/posicion bajo el raton
    ghost_mesh = g3d_primitive_cube();
    ghost_mat = g3d_material_create();
    g3d_material_set_color(ghost_mat, 0.2, 1.0, 0.4, 1.0);
    ghost = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(ghost, ghost_mesh);
    g3d_entity_set_material(ghost, ghost_mat);

    say("1pilar 2hab 3subir 4bajar 5suavizar 6roca 7hierba | CLICK aplicar | +/- pincel");
    x = 480; y = 270;
    write(0, 10, 10, 0, "herramienta:");
    write_var(0, 130, 10, 0, tool);
    write(0, 10, 28, 0, "colocados:");
    write_var(0, 130, 28, 0, placed);

    LOOP
        // Camara: flechas miran, WASD mueve
        IF (key(_LEFT))  yaw = yaw + ts; END
        IF (key(_RIGHT)) yaw = yaw - ts; END
        IF (key(_UP))    pitch = pitch + ts; END
        IF (key(_DOWN))  pitch = pitch - ts; END
        IF (pitch > 85.0)  pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);
        IF (key(_W)) cx = cx + fwx*ms; cy = cy + fwy*ms; cz = cz + fwz*ms; END
        IF (key(_S)) cx = cx - fwx*ms; cy = cy - fwy*ms; cz = cz - fwz*ms; END
        IF (key(_A)) cx = cx + cos(yaw*1000)*ms; cz = cz - sin(yaw*1000)*ms; END
        IF (key(_D)) cx = cx - cos(yaw*1000)*ms; cz = cz + sin(yaw*1000)*ms; END
        IF (key(_Q)) cy = cy - ms; END
        IF (key(_E)) cy = cy + ms; END
        g3d_camera_set_position(camera, cx, cy, cz);
        g3d_camera_look_at(camera, cx+fwx, cy+fwy, cz+fwz,  0.0, 1.0, 0.0);

        // Seleccion de herramienta
        IF (key(_1)) tool = 0; END
        IF (key(_2)) tool = 1; END
        IF (key(_3)) tool = 2; END
        IF (key(_4)) tool = 3; END
        IF (key(_5)) tool = 4; END
        IF (key(_6)) tool = 5; END
        IF (key(_7)) tool = 6; END
        IF (key(_EQUALS)) brush = brush + 0.4; IF (brush > 40.0) brush = 40.0; END END
        IF (key(_MINUS))  brush = brush - 0.4; IF (brush < 2.0) brush = 2.0; END END

        // Raton -> punto del terreno
        hit = g3d_pick_terrain(camera, mouse.x, mouse.y, 960.0, 540.0, ground);
        IF (hit)
            hx = g3d_pick_x(); hz = g3d_pick_z();
            g3d_entity_set_position(ghost, hx, g3d_pick_y() + 1.0, hz);
            g3d_entity_set_scale(ghost, brush, 1.0, brush);

            IF (tool < 2)
                // colocar prefab (solo al hacer click, una vez)
                IF (mouse.left AND prevclick == 0)
                    IF (tool == 0) g3d_prefab_instantiate(pillar, scene, hx, g3d_pick_y(), hz, yaw);
                    ELSE g3d_prefab_instantiate(room, scene, hx, g3d_pick_y(), hz, yaw); END
                    placed = placed + 1;
                END
            ELSE
                // herramientas continuas mientras se mantiene el click
                IF (mouse.left)
                    IF (tool == 2) g3d_terrain_raise(ground, hx, hz, brush, 0.4); END
                    IF (tool == 3) g3d_terrain_raise(ground, hx, hz, brush, -0.4); END
                    IF (tool == 4) g3d_terrain_smooth(ground, hx, hz, brush, 0.3); END
                    IF (tool == 5) g3d_terrain_paint(ground, canvas, rock_tex, 40.0, hx, hz, brush, 0.35); END
                    IF (tool == 6) g3d_terrain_paint(ground, canvas, grass_tex, 40.0, hx, hz, brush, 0.35); END
                END
            END
        END
        prevclick = mouse.left;

        
        

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    
END
