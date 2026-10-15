// main.c -- the launcher's entry point and main loop.
//
// EN: Starts the window and the platform, opens the game's stores, shows the
// start-up films and menu, then runs a frame at a time: input, the script VM,
// the clips, and drawing. Controls: a left click acts on the object under
// the cursor (or walks when there is none), a right click puts up the star
// of actions, F1 is the debug overlay, Tab the extras menu. In a
// conversation, click a line of the menu (or press its number) to say it; a
// click or Space skips the phrase being spoken.
//
// RU: Запускает окно и платформу, открывает архивы игры, показывает стартовые
// ролики и меню, затем работает по кадрам: ввод, машина сценария, клипы,
// отрисовка. Управление: левый щелчок - действие над объектом под курсором
// (или ходьба, если его нет), правый щелчок - звезда действий, F1 - отладочный
// экран, Tab - меню «Дополнительно». В разговоре щелчок по строке меню (или её
// номер) - сказать её; щелчок или пробел пропускают текущую фразу.
#include "game.h"

int main(int argc, char **argv) {
    // With no arguments the game is where the program is: a copy dropped
    // into the game's folder runs it from there, however it was started, and
    // keeps its saved games and settings in that folder too.
    const char *game_dir = argc > 1 ? argv[1] : plat_default_game_dir();
    if (argc <= 1) ChangeDirectory(game_dir);
    const char *part = argc > 2 ? argv[2] : "PART_1";
    const char *digit = part;
    while (*digit && (*digit < '0' || *digit > '9')) digit++;
    if (!*digit) { fprintf(stderr, "usage: %s [game_dir] [PART_N]\n", argv[0]); return 1; }

    static Game game;
    Game *g = &game;
    game_setup(g, game_dir);
    if (!part_open(g, atoi(digit), 1)) return 1;

    InitWindow(GAME_W * 2, GAME_H * 2, "Петька и Василий Иванович");
    // Esc belongs to the game (it backs out of things); the window's own
    // close button ends the program.
    SetExitKey(KEY_NULL);
    plat_init();
    highlight_init();
    s_turbo = getenv("PETKA_TURBO") != NULL;
    SetTargetFPS(s_turbo ? 0 : 60);
    InitAudioDevice();
    Font font = load_cyrillic_font();
    // Dev hooks for unattended runs:
    //   PETKA_SHOT=<file.png>  skip the menu, save a screenshot and quit
    //   PETKA_SHOT_FRAME=<n>   ...after this many frames (default 120)
    //   PETKA_POST=<frame>:<obj_id>:<opcode>[:<arg1>:<arg2>][,...]  post script messages
    //   PETKA_FAST=1           no voice-over, phrases flash by
    //   PETKA_PICK=<n>|last    dialogue menus answer themselves with that line
    //   PETKA_TRACE=1          log delivered script messages to stderr
    //   PETKA_TURBO=1          run on a fixed clock as fast as the machine goes
    //   PETKA_SHOT_SERIES=<start>:<step>:<count>  also save <file>-<frame>.png
    //                          for that many frames, <step> apart
    g->fast = getenv("PETKA_FAST") != NULL;
    const char *pick = getenv("PETKA_PICK");
    g->auto_pick = !pick ? -2 : strcmp(pick, "last") == 0 ? -1 : atoi(pick);
    g->trace = getenv("PETKA_TRACE") != NULL;
    const char *shot_path = getenv("PETKA_SHOT");
    const char *post_plan = getenv("PETKA_POST");
    //   PETKA_PLAY=<file>      play the steps of the file (see play_tick)
    if (getenv("PETKA_PLAY")) {
        play_load(getenv("PETKA_PLAY"));
        g->auto_pick = -2; // the play answers the menus itself
    }
    int unattended = shot_path != NULL || s_play.steps != NULL;
    long shot_frame = getenv("PETKA_SHOT_FRAME") ? atol(getenv("PETKA_SHOT_FRAME")) : 120;
    long frame = 0;
    // The exe opens with three films, one after another (sub_410CD0): the
    // publisher's and the developer's marks and an advertisement.
    if (!unattended) {
        play_film(g, "buka.avi");
        play_film(g, "skif.avi");
        play_film(g, "adv.avi");
    }
    int menu = unattended && !getenv("PETKA_MENU") ? 1 : run_menu(g, font);
    if (g->quit || !menu) { plat_close(); CloseWindow(); return 0; }

    if (menu == 2) {
        // "Load": the script is set going, and the save screen comes first.
        game_init(g);
        g->boot_load = 1;
        saves_open(g, 1);
    } else {
        game_start(g);
    }

    int debug = getenv("PETKA_DEBUG") != NULL; // dev: start with the F1 overlay on
    // The extras as last set -- not in an unattended dev run, which keeps
    // to its defaults.
    g->opt_aim = 1;
    // On by default -- but not in an unattended dev run, whose screenshots
    // are compared pixel for pixel with older ones.
    g->opt_highlight = !unattended || getenv("PETKA_HIGHLIGHT");
    if (!unattended) extras_load(g);
    int crt_saved = crt_on();
    Drawable scene[256];
    // ==== The main loop: a frame at a time ====
    // EN: Each frame: input (the extras menu first), a change of part or a
    // return to the start-up menu, the script's turn, the clips, what the
    // player does, then drawing.
    // RU: Каждый кадр: ввод (сначала меню «Дополнительно»), смена части или
    // возврат в стартовое меню, ход сценария, клипы, действия игрока, затем
    // отрисовка.
    while (!WindowShouldClose() && !g->quit) {
        float scale = (float)plat_width() / GAME_W;
        plat_poll();
        Vector2 mouse = plat_pointer();
        // dev: PETKA_MOUSE=<x>,<y> pins the pointer (game pixels) for
        // unattended runs, so hovering can be screenshotted.
        float pin_x, pin_y;
        if (getenv("PETKA_MOUSE") && sscanf(getenv("PETKA_MOUSE"), "%f,%f", &pin_x, &pin_y) == 2)
            mouse = (Vector2){pin_x * scale, pin_y * scale};
        // The extras menu has the input first; while it is up the game has
        // none of it.
        if (g->extras_open) {
            extras_update(g, mouse, scale, &debug);
            plat_mute();
        } else if (plat_key(KEY_TAB) || (getenv("PETKA_EXTRAS") && atol(getenv("PETKA_EXTRAS")) == frame)) {
            // dev: PETKA_EXTRAS=<frame> opens it on that frame of an unattended run.
            g->extras_open = 1;
            g->extras_focus = 0;
            plat_mute();
        }
        // F4 (a press of the left stick) switches the CRT look by itself.
        if (crt_on() != crt_saved) {
            crt_saved = crt_on();
            if (!unattended) extras_save(g);
        }
        // dev: PETKA_CLICK=<frame>:<x>:<y>[:<verb>][,...] left-clicks at a
        // point of the screen (game pixels), optionally picking a verb first.
        int clicked = plat_click();
        for (const char *p = getenv("PETKA_CLICK"); p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "") {
            long at = 0;
            float cx = 0, cy = 0;
            int verb = -1;
            if (sscanf(p, "%ld:%f:%f:%d", &at, &cx, &cy, &verb) < 3 || at != frame + 1) continue;
            mouse = (Vector2){cx * scale, cy * scale};
            clicked = 1;
            if (verb >= 0 && verb < VERB_COUNT) g->verb = verb;
        }
        // The pointer in background pixels: where it is in the room.
        float mx = mouse.x / scale + g->camera_x, my = mouse.y / scale;
        // Another part was asked for -- by "new game", the script's PART or
        // a saved game. Nothing of this frame holds the old one yet.
        // "Load" at start-up put the save screen up before any room: left
        // without loading, it gives way to the screen it came from -- the
        // start-up one again (the exe's screens go back to the previous).
        // ---- EN: the start-up menu after the end; another part / RU: стартовое меню после конца игры; другая часть ----
        if (g->to_menu) {
            // The game is over: everything of it is silenced and the start-up
            // menu comes up, as at the start.
            g->to_menu = 0;
            set_stream(g, &g->music, &g->music_id, PETKA_NONE);
            set_stream(g, &g->ambience, &g->ambience_id, PETKA_NONE);
            set_stream(g, &g->seq_music, &g->seq_music_id, PETKA_NONE);
            set_stream(g, &g->seq_ambience, &g->seq_ambience_id, PETKA_NONE);
            for (int i = 0; i < MAX_SOUNDS; i++) {
                if (g->sounds[i].frameCount > 0) StopSound(g->sounds[i]);
                g->sound_paused[i] = 0;
            }
            if (g->voice.frameCount > 0) StopSound(g->voice);
            // The game ends in a sequence, where a controller's pointer is
            // hidden and held: the menu has it free again, in the middle.
            if (plat_pad()) {
                g->pad_hidden = g->pad_hold = 0;
                plat_hold_pointer(false);
                plat_set_pointer((Vector2){plat_width() / 2.0f, plat_height() / 2.0f});
            }
            int again = run_menu(g, font);
            if (g->quit || !again) break;
            if (again == 2) {
                // As "load" at start-up: the script set going afresh, no
                // room yet, and the save screen over it -- not over the end
                // still running, which hid the pointer and in a while took
                // the save screen down.
                game_screens_down(g);
                game_init(g);
                g->boot_load = g->menu_on_back = 1;
                saves_open(g, 1);
            } else {
                new_game(g);
            }
            continue;
        }
        if (g->boot_load && !g->save_mode && !g->switch_part) {
            g->boot_load = 0;
            if (g->menu_on_back) {
                // The save screen of the menu after the end, left without
                // loading: the menu again.
                g->menu_on_back = 0;
                g->to_menu = 1;
                continue;
            }
            if (g->vm->room == PETKA_NONE) {
                int again = run_menu(g, font);
                if (g->quit || !again) break;
                if (again == 2) {
                    g->boot_load = 1;
                    saves_open(g, 1);
                    continue;
                }
                game_enter(g);
            }
        }
        if (g->switch_part) {
            int to_part = g->switch_part, to_chapter = g->switch_chapter > 0 ? g->switch_chapter : 1;
            g->switch_part = 0;
            game_switch(g, to_part, to_chapter);
            if (g->pending_load[0]) {
                char save_file[64];
                snprintf(save_file, sizeof(save_file), "%s", g->pending_load);
                g->pending_load[0] = 0;
                // Only if the part is the save's now: no going round in circles.
                if (g->part_number == to_part && game_load(g, save_file)) set_status(g, "(игра загружена)");
            }
        }
        PetkaScriptVM *vm = g->vm;

        if (plat_key(KEY_F1)) debug = !debug;
        // The cursor's action keys, from the exe's accelerator table: a digit
        // and a letter each (note 3 is "take" and 4 is "use"). The exe's
        // handlers do nothing unless the panel is switched on, and they put
        // the action's own clip on the cursor, dropping an item it carried.
        static const struct { int digit, letter, action; } kActionKeys[] = {
            {KEY_ONE, KEY_L, 0}, {KEY_TWO, KEY_W, 1}, {KEY_THREE, KEY_G, 3},
            {KEY_FOUR, KEY_U, 2}, {KEY_FIVE, KEY_T, 4}, {KEY_SIX, KEY_C, 5},
        };
        // (A description on screen switches the panel off, too.)
        for (int k = 0; k < 6 && !g->menu_count && vm->interface_enabled && !g->map_open && !g->desc[0]; k++) {
            if (!plat_key(kActionKeys[k].digit) && !plat_key(kActionKeys[k].letter)) continue;
            g->verb = kActionKeys[k].action;
            drop_item(g);
        }

        // ---- EN: the script's turn / RU: ход сценария ----
        frame++;
        s_clock += 1.0 / 60.0;
        // A mark a second, so a log shows a hang outside the script's turn.
        if (g->trace && frame % 60 == 0)
            fprintf(stderr, "frame %ld: room %u seq %u\n", frame, vm->room, (unsigned)g->seq_room);
        for (const char *p = post_plan; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : "") {
            long at = 0;
            unsigned obj_id = 0, opcode = 0, arg1 = PETKA_NONE, arg2 = PETKA_NONE;
            if (sscanf(p, "%ld:%u:%u:%u:%u", &at, &obj_id, &opcode, &arg1, &arg2) >= 3 && at == frame)
                petka_scriptvm_post(vm, (uint16_t)obj_id, (uint16_t)opcode, (uint16_t)arg1, (uint16_t)arg2,
                                    PETKA_NONE, -1);
        }

        if (g->dialogue_done) {
            g->dialogue_done = 0;
            // The VM sends the last speaker his SAID before going on.
            uint16_t talker = g->talker;
            g->talker = PETKA_NONE;
            petka_scriptvm_dialogue_ended(vm, talker);
        }
        if (petka_scriptvm_run(vm) < 0) set_status(g, "(сценарий зациклился)");

        // The room the VM is in may have changed: drop the old room's clips.
        // Not those of the game's room while a sequence is shown over it:
        // they run on under it (see below), and dropped they would start
        // over -- part 2's pentagram, a frame behind Petka's clip whose END
        // put up the sequence, played all over again before the sequence
        // could go on.
        if (g->shown_room != screen_room(g)) {
            const PetkaObject *keep = g->seq_room != PETKA_NONE && !g->map_open
                                    ? petka_script_find(g->script, vm->room) : NULL;
            for (uint32_t i = 0; i < g->script->object_count; i++) {
                uint16_t id = g->script->objects[i].id;
                if (is_hero(id)) continue;
                int kept = 0;
                for (uint32_t k = 0; keep && k < keep->attached_count && !kept; k++)
                    kept = keep->attached[k].obj_id == id;
                if (!kept) visual_free(&g->visuals[i]);
            }
            g->shown_room = screen_room(g);
        }
        const PetkaObject *room = petka_script_find(g->script, screen_room(g));
        const PetkaObjectState *room_st = room ? &vm->state[obj_index(g, room)] : NULL;
        if (room_st && room_st->resource != g->bg_resource) {
            if (g->bg_tex.id) UnloadTexture(g->bg_tex);
            g->bg_tex = (Texture2D){0};
            g->bg_resource = room_st->resource;
            const char *path = g->bg_resource != PETKA_NONE ? g->qrc[g->bg_resource] : NULL;
            if (path && has_ext(path, ".BMP")) g->bg_tex = load_bmp_texture(g, path);
        }
        load_mesh(g);
        if (room_st) {
            // The map and the panel leave the room's tracks playing. A
            // sequence does not (its set-up, sub_416860): the game's sounds
            // are paused where they are, and the sequence room's own music
            // and ambience play instead -- kept going if they are the ones
            // the last sequence played, started if they are others. When
            // it ends the room's tracks go on from where they stopped.
            int sequence = g->seq_room != PETKA_NONE && !g->map_open;
            if (sequence != g->tracks_hushed) {
                if (g->music.frameCount > 0) { if (sequence) PauseMusicStream(g->music); else ResumeMusicStream(g->music); }
                if (g->ambience.frameCount > 0) {
                    if (sequence) PauseMusicStream(g->ambience);
                    else ResumeMusicStream(g->ambience);
                }
                g->tracks_hushed = sequence;
            }
            if (sequence) {
                set_stream(g, &g->seq_music, &g->seq_music_id, room_st->music);
                set_stream(g, &g->seq_ambience, &g->seq_ambience_id, room_st->bgsfx);
                if (g->seq_tracks_paused) {
                    if (g->seq_music.frameCount > 0) ResumeMusicStream(g->seq_music);
                    if (g->seq_ambience.frameCount > 0) ResumeMusicStream(g->seq_ambience);
                    g->seq_tracks_paused = 0;
                }
            } else {
                if (!g->seq_tracks_paused) {
                    if (g->seq_music.frameCount > 0) PauseMusicStream(g->seq_music);
                    if (g->seq_ambience.frameCount > 0) PauseMusicStream(g->seq_ambience);
                    g->seq_tracks_paused = 1;
                }
                const PetkaObjectState *under = g->map_open ? petka_scriptvm_state(vm, vm->room) : NULL;
                uint16_t music = room_st->music, ambience = room_st->bgsfx;
                if (under && music == PETKA_NONE) music = under->music;
                if (under && ambience == PETKA_NONE) ambience = under->bgsfx;
                set_stream(g, &g->music, &g->music_id, music);
                set_stream(g, &g->ambience, &g->ambience_id, ambience);
            }
        }
        Music *streams[4] = {&g->music, &g->seq_music, &g->ambience, &g->seq_ambience};
        for (int i = 0; i < 4; i++) {
            if (streams[i]->frameCount <= 0) continue;
            SetMusicVolume(*streams[i], dial_gain(g, i < 2 ? DIAL_MUSIC : DIAL_SFX));
            UpdateMusicStream(*streams[i]);
        }

        // ---- EN: the view and the clips / RU: вид и клипы ----
        camera_update(g, game_dt());
        // Everything in the room is drawn through this; the HUD is not.
        Camera2D view = {.offset = {0, 0}, .target = {roundf(g->camera_x) * scale, 0}, .rotation = 0, .zoom = 1};
        // SHAKE: the exe puts the finished frame on screen 3 pixels to the
        // right on alternate 30 ms ticks, with a black strip where it was.
        if (vm->shake && ((long)(game_now() * 1000.0 / 30.0) & 1)) view.target.x -= 3 * scale;

        for (uint32_t i = 0; i < g->script->object_count; i++) g->visuals[i].live = 0;
        for (uint32_t i = 0; room && i < room->attached_count; i++) {
            const PetkaObject *obj = petka_script_find(g->script, room->attached[i].obj_id);
            if (obj) visual_update(g, obj);
        }
        for (uint16_t id = 0; id < 2 && g->seq_room == PETKA_NONE; id++) {
            const PetkaObject *hero = petka_script_find(g->script, id);
            if (hero && !g->visuals[obj_index(g, hero)].live) visual_update(g, hero);
        }
        // A sequence is shown in place of the game's room, but that room
        // goes on: its clips keep running out of sight, unheard, and the
        // script may be waiting on them. In part 2 the pentagram in the
        // church, started with Petka's clip there, carries the story on at
        // its END -- and Petka's END, which puts up the sequence, can come
        // first: then the pentagram stood still under it and the game with
        // it. (Not under the map or the panel, which are no sequences.)
        if (g->seq_room != PETKA_NONE && !g->map_open) {
            const PetkaObject *under = petka_script_find(g->script, vm->room);
            g->clips_quiet = 1;
            for (uint32_t i = 0; under && i < under->attached_count; i++) {
                const PetkaObject *obj = petka_script_find(g->script, under->attached[i].obj_id);
                if (obj && !g->visuals[obj_index(g, obj)].live) visual_update(g, obj);
            }
            g->clips_quiet = 0;
        }
        // The things in the case have clips of their own, and the script
        // waits for some of them: in part 2 the pen on the cheque hides the
        // cursor, sets the cheque writing itself and gives the cursor back
        // on that clip's END. So they are kept going like the room's
        // objects, the case open or shut.
        for (uint32_t i = 0; i < vm->inventory_count; i++) {
            const PetkaObject *thing = petka_script_find(g->script, vm->inventory[i]);
            if (thing && !g->visuals[obj_index(g, thing)].live) visual_update(g, thing);
        }
        // A PLAY aimed at something that is not on screen (hidden, another
        // room, missing clip) would never end; end it at once instead.
        for (uint32_t i = 0; i < g->script->object_count; i++) {
            const PetkaObjectState *st = &vm->state[i];
            if (st->play_tail.present && (!g->visuals[i].live || !st->visible))
                petka_scriptvm_animation_ended(vm, g->script->objects[i].id, st->play_wait);
        }
        if (petka_scriptvm_run(vm) < 0) set_status(g, "(сценарий зациклился)");

        if (g->panel_open) panel_show_state(g);
        // ---- EN: the scene and the screens' keys / RU: сцена и клавиши экранов ----
        int scene_count = build_scene(g, scene, (int)(sizeof(scene) / sizeof(scene[0])));
        // Alt+R (the exe's command 32787, 0x4181F0) gets the player out of
        // a game left with no cursor: the dialogue's pause is lifted, the
        // cursor shown and the star -- the whole interface -- switched on.
        if ((plat_key_down(KEY_LEFT_ALT) || plat_key_down(KEY_RIGHT_ALT)) && plat_key(KEY_R)) {
            g->paused = 0;
            vm->cursor_visible = 1;
            vm->interface_enabled = 1;
        }
        // Esc (the exe's command 32786, 0x418190): the case is shut, and
        // the screen that is up is left -- the map, the panel and the save
        // screen go back to the game; the game's own screen only takes a
        // description down. dev: PETKA_ESC=<frame> presses it.
        int escape = plat_key(KEY_ESCAPE) || (getenv("PETKA_ESC") && atol(getenv("PETKA_ESC")) == frame);
        int desc_shown = g->desc[0] != 0;
        int shut_desc = 0;
        if (escape) {
            g->case_open = 0;
            if (g->save_mode) saves_close(g);
            else if (g->map_open) map_leave(g);
            else if (desc_shown) shut_desc = 1;
        }
        // A description stays up until a click, which it takes. Shutting
        // it (sub_414AC0) switches the panel on, whatever it was before.
        if (desc_shown && (shut_desc || clicked || plat_rclick() || plat_key(KEY_SPACE))) {
            g->desc[0] = 0;
            vm->interface_enabled = 1;
            clicked = 0;
        }
        int can_act = vm->cursor_visible && !g->talking && (g->seq_room == PETKA_NONE || g->map_open) &&
                      !g->save_mode && !desc_shown;
        if (s_play.steps) play_tick(g, can_act);
        // The save screen takes the mouse to itself. Esc leaves it, as in
        // the original; so, here, do the right button and O.
        if (g->save_mode) {
            if (plat_rclick() || plat_key(KEY_O)) saves_close(g);
            else if (clicked) saves_click(g, (Vector2){mouse.x / scale, mouse.y / scale});
        }
        // dev: PETKA_SAVE=<frame> and PETKA_LOAD=<frame> write and read
        // save1.dat on that frame of an unattended run.
        if (getenv("PETKA_SAVE") && atol(getenv("PETKA_SAVE")) == frame) {
            take_thumb(g);
            fprintf(stderr, "save: %s\n", game_save(g, "save1.dat") ? "ok" : "failed");
        }
        if (getenv("PETKA_LOAD") && atol(getenv("PETKA_LOAD")) == frame)
            fprintf(stderr, "load: %s\n", game_load(g, "save1.dat") ? "ok" : "failed");
        // The case is opened and shut with I (the exe's command 32779),
        // while the panel is on. Open, it takes the mouse off the room.
        // dev: PETKA_CASE=<frame> opens it on that frame of an unattended run.
        int case_key = plat_key(KEY_I) || (getenv("PETKA_CASE") && atol(getenv("PETKA_CASE")) == frame);
        if (case_key && can_act && vm->interface_enabled && !g->menu_count && !g->map_open)
            g->case_open = !g->case_open;
        while (g->case_first > 0 && (uint32_t)g->case_first >= vm->inventory_count) g->case_first -= CASE_PAGE;
        if (g->case_first < 0) g->case_first = 0;
        // dev: PETKA_MAP=<frame> presses M on that frame of an unattended run.
        int map_key = plat_key(KEY_M) ||
                      (getenv("PETKA_MAP") && atol(getenv("PETKA_MAP")) == frame);
        if (map_key && can_act && !g->menu_count) map_command(g);
        // dev: PETKA_PANEL=<frame> presses O on that frame of an unattended run.
        int panel_key = plat_key(KEY_O) || (getenv("PETKA_PANEL") && atol(getenv("PETKA_PANEL")) == frame);
        if (panel_key && can_act && !g->menu_count) panel_command(g);
        // F2 and F3 (the exe's commands 32771 and 32772, 0x417B80 and
        // 0x417C00) go straight from the game's own screen to the save
        // screen: F2 to save -- only while the star is active; the case is
        // shut and the slot picture taken first -- and F3 to load.
        int on_main = can_act && !g->menu_count && !g->map_open && !g->star_open;
        if (plat_key(KEY_F2) && on_main && vm->interface_enabled) {
            g->case_open = 0;
            take_thumb(g);
            saves_open(g, 2);
        }
        if (plat_key(KEY_F3) && on_main) saves_open(g, 1);
        // The star is up only while the player can act, and while it is up
        // nothing else is under the pointer.
        if (!can_act || g->map_open) g->star_open = 0;
        // ---- EN: what is under the pointer / RU: что под указателем ----
        // With a controller the case's pointer is on one of its things (a
        // touch on one takes it); the star, while up, keeps it where it is.
        if (plat_pad() && g->case_open && can_act && !g->star_open) {
            Vector2 at = {mouse.x / scale, mouse.y / scale};
            for (int k = 0; k < CASE_PAGE; k++) {
                Visual *v = NULL;
                if (case_item(g, k, &v) && clip_hit(v, (int)at.x - kCaseCell[k][0], (int)at.y - kCaseCell[k][1]))
                    g->case_focus = k;
            }
            for (int i = 0; i < 3; i++) {
                const PetkaMskRect *r = &g->case_button[i].rect;
                if (g->case_button[i].flic && at.x >= r->left && at.y >= r->top && at.x < r->right && at.y < r->bottom)
                    g->case_focus = CASE_PAGE + i;
            }
            at = case_steer(g, plat_list_step_x(), plat_list_step(), plat_page_step());
            mouse = (Vector2){at.x * scale, at.y * scale};
            plat_set_pointer(mouse);
            mx = mouse.x / scale + g->camera_x;
            my = mouse.y / scale;
        }
        // With a controller the pointer is drawn to things nearby.
        // Not while L makes the cursor walk: then the floor beside a thing
        // is what is aimed at.
        if (plat_pad() && g->opt_aim && !g->extras_open && can_act && !g->case_open && !g->star_open &&
            !g->menu_count && !plat_walk_held()) {
            Vector2 at = aim_assist(g, scene, scene_count, (Vector2){mx, my});
            if (at.x != mx || at.y != my) {
                mx = at.x;
                my = at.y;
                mouse = (Vector2){(mx - g->camera_x) * scale, my * scale};
                plat_set_pointer(mouse);
            }
        } else if (plat_pad()) {
            plat_pointer_speed(1.0f);
        }
        int screen_x = (int)(mouse.x / scale), screen_y = (int)(mouse.y / scale);
        // R held: the cursor sends Chapayev; L held: it walks; otherwise a
        // controller works the star. (In the case L and R turn pages.)
        int held = plat_pad() && vm->cursor_object == PETKA_NONE && !g->case_open;
        int send_chapayev = held && plat_chapayev_held();
        int send_walk = held && !send_chapayev && plat_walk_held();
        int pad_star = plat_pad() && !send_chapayev && !send_walk;
        if (send_chapayev || send_walk) g->star_open = 0;
        int verb = send_chapayev ? VERB_COUNT - 1 : send_walk ? 1 : g->verb;
        g->star_lit = !g->star_open ? 6 : pad_star ? star_aim(!g->case_open) : star_part(g, screen_x, screen_y);
        // dev: PETKA_RCLICK=<frame> presses the right button on that frame.
        int right_click = plat_rclick() ||
                          (getenv("PETKA_RCLICK") && atol(getenv("PETKA_RCLICK")) == frame);
        if (!g->case_open) g->case_for = PETKA_NONE;
        int in_case = g->case_open && can_act && !g->star_open;
        if (!in_case) g->case_lit = -1;
        const PetkaObject *hover = !can_act || g->star_open ? NULL
                                 : in_case ? case_hover(g, (int)(mouse.x / scale), (int)(mouse.y / scale))
                                 : g->map_open ? listed_object_at(g, mx, my)
                                 : object_at(g, scene, scene_count, mx, my);

        g->panel_hover = -1;
        if (g->panel_open && hover) {
            const PetkaObject *panel = petka_script_find(g->script, g->seq_room);
            for (uint32_t i = 0; panel && i < panel->attached_count; i++)
                if (panel->attached[i].obj_id == hover->id) g->panel_hover = (int)i;
        }
        // ---- EN: the dialogue's menu, phrases, and the player's clicks / RU: меню реплик, фразы и щелчки игрока ----
        int menu_hover = -1;
        Rectangle menu_rects[MENU_MAX];
        if (g->menu_count > 0) menu_lines(g, font, scale, menu_rects, -1, 0);
        for (int i = 0; i < g->menu_count && i < MENU_MAX; i++)
            if (CheckCollisionPointRec(mouse, menu_rects[i])) menu_hover = i;
        if (plat_pad()) {
            int lines = g->menu_count < MENU_MAX ? g->menu_count : MENU_MAX;
            int in_dialogue = lines > 0 || g->talking || g->paused;
            if (in_dialogue && !g->pad_hold) {
                g->pad_hold = 1;
                g->menu_focus = 0;
                plat_hold_pointer(true);
            }
            if (lines > 0) {
                // A touch on a line takes it; up and down step from it.
                if (menu_hover >= 0) g->menu_focus = menu_hover;
                // Past either end it comes round to the other.
                g->menu_focus = (g->menu_focus + plat_list_step() + lines) % lines;
                // The pointer sits at the right end of the line's first row,
                // where it hides the least of the text.
                Rectangle r = menu_rects[g->menu_focus];
                mouse = (Vector2){r.x + r.width - 1, r.y + 10 * scale};
                plat_set_pointer(mouse);
                menu_hover = g->menu_focus;
            } else if (g->pad_hold && !in_dialogue) {
                // (Back where it was: see pad_hidden.)
                g->pad_hold = 0;
            }
        }
        if (g->menu_count > 0) {
            // The dialogue waits for the player's line: click it or press
            // its number.
            int pick = clicked ? menu_hover : -1;
            for (int k = 0; k < g->menu_count && k < 9; k++) if (plat_key(KEY_ONE + k)) pick = k;
            if (pick >= 0 && petka_dialoguevm_choose(&g->dvm, pick) == 0) {
                g->menu_count = 0;
                next_phrase(g);
            }
        } else if (g->talking) {
            int voiced = g->voice.frameCount > 0;
            // With a controller B skips a phrase: A, pressed again to pick
            // a line of the menu, would skip the first one said after it.
            int skip = plat_pad() ? right_click : clicked;
            if (skip || plat_key(KEY_SPACE) ||
                (voiced ? !IsSoundPlaying(g->voice) : game_now() >= g->line_until))
                next_phrase(g);
        } else if (can_act) {
            // The right button (sub_414AF0), while the panel is on: with
            // the case open and a thing on the cursor, the thing is put down
            // and the cursor becomes "take"; otherwise the star comes up at
            // the pointer, or goes away.
            if (right_click && plat_pad()) {
                // B: the star down, the case shut (a thing taken stays on
                // the cursor), or the thing on the cursor put back.
                if (g->star_open) {
                    g->star_open = 0;
                } else if (g->case_open) {
                    g->case_open = 0;
                } else if (vm->cursor_object != PETKA_NONE) {
                    drop_item(g);
                }
            } else if (right_click && !g->map_open && vm->interface_enabled) {
                if (g->case_open && vm->cursor_object != PETKA_NONE) {
                    drop_item(g);
                    g->verb = 3;
                } else {
                    star_toggle(g, screen_x, screen_y);
                }
            }
            if (clicked) {
                if (g->star_open) {
                    if (pad_star) star_pick(g);
                    else star_click(g, g->star_lit);
                } else if (g->panel_open) {
                    if (hover) panel_click(g, hover);
                } else if (hover && in_case && g->case_for != PETKA_NONE) {
                    // A thing picked for the object: used on it, as if
                    // taken and clicked there.
                    uint16_t target = g->case_for;
                    g->case_open = 0;
                    g->case_for = PETKA_NONE;
                    drop_item(g);
                    stop_heroes(g);
                    petka_scriptvm_post(vm, target, PETKA_OP_OBJECTUSE, 0, 0, 0, hover->id);
                } else if (hover && vm->cursor_object != PETKA_NONE) {
                    uint16_t item = vm->cursor_object;
                    stop_heroes(g);
                    drop_item(g);
                    petka_scriptvm_post(vm, hover->id, PETKA_OP_OBJECTUSE, 0, 0, 0, item);
                } else if (hover && pad_star && !g->map_open && vm->interface_enabled) {
                    star_toggle(g, screen_x, screen_y);
                    g->pad_target = hover->id;
                    g->pad_x = mx;
                    g->pad_y = my;
                } else if (hover) {
                    act_on(g, hover, verb, mx, my);
                } else if (in_case) {
                    case_click(g);
                } else if (vm->cursor_object == PETKA_NONE && !g->map_open) {
                    // Nothing under the click (the end of sub_414320): only
                    // two of the cursor's actions do anything. The
                    // controller's plain pointer walks.
                    uint16_t op = pad_star ? PETKA_OP_WALK : kVerbs[verb].op;
                    if (op == PETKA_OP_WALK) {
                        PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
                        // Sent somewhere else: whatever he was walking for is
                        // off, and if that was a way out of the room,
                        // Chapayev stops following him there.
                        int leaving = 0;
                        for (uint32_t i = 0; petka && i < petka->walk_tail.count; i++)
                            if (petka->walk_tail.messages[i].opcode == PETKA_OP_GOTO) leaving = 1;
                        if (leaving) petka_scriptvm_stop_hero(vm, PETKA_ID_CHAPAYEV);
                        petka_scriptvm_forget_walk(vm, PETKA_ID_PETKA);
                        if (petka && host_walk(g, PETKA_ID_PETKA, (int32_t)mx, (int32_t)my)) petka->busy = 1;
                    } else if (op == PETKA_OP_OBJECTUSE) {
                        // "Send Chapayev" at the floor: he walks there.
                        PetkaObjectState *chapayev = petka_scriptvm_state(vm, PETKA_ID_CHAPAYEV);
                        if (chapayev && host_walk(g, PETKA_ID_CHAPAYEV, (int32_t)mx, (int32_t)my)) chapayev->busy = 1;
                    }
                }
            }
        }

        // The stick works the star while it is up, not the pointer.
        if (plat_pad())
            plat_hold_pointer(g->pad_hold || g->pad_hidden || g->star_open || g->extras_open ||
                              (g->case_open && can_act));

        // ---- EN: drawing / RU: отрисовка ----
        plat_begin();
        ClearBackground(BLACK);
        BeginMode2D(view);
        if (g->bg_tex.id)
            DrawTexturePro(g->bg_tex, (Rectangle){0, 0, (float)g->bg_tex.width, (float)g->bg_tex.height},
                           (Rectangle){0, 0, g->bg_tex.width * scale, g->bg_tex.height * scale}, (Vector2){0, 0}, 0, WHITE);
        // The thing under the pointer gets a yellow outline (the extras) --
        // drawn or painted on the background, not a character -- while the
        // player can act in the room itself.
        int outline = g->opt_highlight && can_act && !g->map_open && g->seq_room == PETKA_NONE;
        for (int i = 0; i < scene_count; i++) {
            const Visual *v = &g->visuals[obj_index(g, scene[i].obj)];
            Rectangle d = visual_dest(g, scene[i].obj);
            int shown = vm->state[obj_index(g, scene[i].obj)].visible;
            Rectangle at = {d.x * scale, d.y * scale, d.width * scale, d.height * scale};
            // On the map a place's clip is its highlight: the script keeps it
            // hidden, and the map's mouse function (sub_41D4D0) shows the one
            // under the pointer, on its first frame, and hides the rest.
            if (shown || (g->map_open && !g->panel_open && scene[i].obj == hover)) visual_draw(v, at);
            // Drawn with the thing, in z order: what stands nearer covers it.
            if (outline && scene[i].obj == hover && highlight_wanted(g, scene[i].obj)) highlight_draw(v, at);
            if (debug && !is_hero(scene[i].obj->id)) {
                Rectangle r = {v->rect.left * scale, v->rect.top * scale,
                               (v->rect.right - v->rect.left) * scale, (v->rect.bottom - v->rect.top) * scale};
                DrawRectangleLinesEx(r, 1, scene[i].obj == hover ? ORANGE : Fade(shown ? LIME : MAGENTA, 0.6f));
                DrawTextEx(font, TextFormat("%u %s", scene[i].obj->id, utf8_name(scene[i].obj->name)),
                           (Vector2){r.x + 2, r.y + 2}, 14, 1, YELLOW);
            }
        }

        EndMode2D();

        if (g->case_open) draw_case(g, scale);
        if (g->star_open) draw_star(g, scale);
        if (g->save_mode) draw_saves(g, font, scale);

        // Top bar: what a click would do.
        // The object under the cursor is captioned as the exe does it (the
        // end of 0x414020): its NAMES.INI name in a pale cyan, or, where it
        // has none, its script name in dark red.
        const char *caption = "";
        Color caption_color = {192, 255, 255, 255};
        if (hover) {
            const char *shown = g->names[hover - g->script->objects];
            if (shown && shown[0]) {
                caption = utf8_name(shown);
            } else {
                caption = utf8_name(hover->name);
                caption_color = (Color){128, 0, 0, 255};
            }
        }
        const char *action;
        if (!vm->cursor_visible || (g->seq_room != PETKA_NONE && !g->map_open)) {
            action = "...";
            caption = "";
        } else if (vm->cursor_object != PETKA_NONE) {
            const PetkaObject *item = petka_script_find(g->script, vm->cursor_object);
            action = TextFormat("%s -> %s", item ? display_name(g, item) : "?", hover ? "" : "...");
        } else {
            action = kVerbs[g->verb].label;
        }
        // The bar along the top (the cursor's action, the room's name) is
        // the launcher's own, not the original's: shown with F1 only.
        if (debug) {
            DrawRectangle(0, 0, plat_width(), 30, Fade(BLACK, 0.6f));
            DrawTextEx(font, action, (Vector2){10, 5}, 20, 1, WHITE);
        }
        // (The panel's mouse function names nothing: no captions there.)
        if (caption[0] && !g->panel_open) draw_bottom_text(font, caption, caption_color, (Color){10, 10, 10, 255}, 0, 1, scale);
        if (room && debug) {
            const char *name = display_name(g, room);
            Vector2 size = MeasureTextEx(font, name, 18, 1);
            DrawTextEx(font, name, (Vector2){plat_width() - size.x - 10, 6}, 18, 1, SKYBLUE);
        }

        if (g->talking && g->line[0] && g->subtitles) {
            // Each speaker's lines are in his own colour.
            const PetkaObject *talker = petka_script_find(g->script, g->talker);
            // A speaker with a colour of his own (CAST.INI) is drawn on
            // 0x00007F, dark red; one without in 0x0A0A0A on white.
            Color ink = talker ? g->colors[obj_index(g, talker)] : NO_CAST_INK;
            int cast = ink.r != NO_CAST_INK.r || ink.g != NO_CAST_INK.g || ink.b != NO_CAST_INK.b;
            draw_bottom_text(font, g->line, ink, cast ? (Color){127, 0, 0, 255} : WHITE, 1, 1, scale);
        }
        if (g->desc[0]) draw_description(g, font, scale);

        if (g->menu_count > 0) menu_lines(g, font, scale, menu_rects, menu_hover, 1);

        if (g->status[0] && game_now() < g->status_until) {
            // The launcher's own messages, top left (higher with the bar
            // of the F1 overlay off): white, on a band like the rest.
            Vector2 at = {10 + 6 * scale, debug ? 36 : 6 * scale};
            Vector2 extent = MeasureTextEx(font, g->status, 18, 1);
            draw_band((Rectangle){at.x, at.y, extent.x, extent.y}, scale);
            DrawTextEx(font, g->status, at, 18, 1, WHITE);
        }
        if (debug) {
            // The floor's polygons, and the route each hero is following.
            BeginMode2D(view);
            for (uint32_t i = 0; g->mesh && i < g->mesh->poly_count; i++) {
                const PetkaWalkPoly *poly = &g->mesh->polys[i];
                for (uint32_t k = 0; k < poly->count; k++) {
                    const PetkaPoint *a = &g->mesh->points[poly->verts[k]];
                    const PetkaPoint *b = &g->mesh->points[poly->verts[(k + 1) % poly->count]];
                    DrawLineEx((Vector2){a->x * scale, a->y * scale}, (Vector2){b->x * scale, b->y * scale},
                               1, Fade(SKYBLUE, 0.8f));
                }
            }
            for (uint16_t id = 0; id < 2; id++) {
                const Walk *w = &g->walk[id];
                const PetkaObjectState *hero = petka_scriptvm_state(vm, id);
                Vector2 from = {hero ? hero->x * scale : 0, hero ? hero->y * scale : 0};
                for (int i = w->walker.next; w->active && hero && i < w->walker.count; i++) {
                    Vector2 to = {w->walker.route[i].x * scale, w->walker.route[i].y * scale};
                    DrawLineEx(from, to, 3, id ? MAGENTA : RED);
                    DrawCircleV(to, 5, id ? MAGENTA : RED);
                    from = to;
                }
            }
            EndMode2D();
            const PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
            DrawTextEx(font, TextFormat("room %u  objects %d  queue %u  view %d  mouse %d,%d  petka %d,%d %s",
                                        vm->room, scene_count, vm->queue_count, (int)g->camera_x, (int)mx, (int)my,
                                        petka ? petka->x : 0, petka ? petka->y : 0,
                                        petka && petka->visible ? "" : "(hidden)"),
                       (Vector2){10, 58}, 16, 1, LIME);
            // What decides whether a click does anything, for a screenshot
            // of a game that has stopped answering.
            const PetkaObjectState *vich = petka_scriptvm_state(vm, PETKA_ID_CHAPAYEV);
            DrawTextEx(font, TextFormat("petka vis %d act %d anim %d busy %d walk %d tail %u res %u | vich vis %d act %d busy %d walk %d",
                                        petka ? petka->visible : -1, petka ? petka->active : -1, petka ? petka->animate : -1,
                                        petka ? petka->busy : -1, g->walk[0].active, petka ? petka->walk_tail.count : 0,
                                        petka ? petka->resource : 0, vich ? vich->visible : -1, vich ? vich->active : -1,
                                        vich ? vich->busy : -1, g->walk[1].active),
                       (Vector2){10, 78}, 16, 1, LIME);
            DrawTextEx(font, TextFormat("cursor %d panel %d verb %d item %d | talk %d pause %d menu %d seq %u map %d case %d star %d save %d | hover %d",
                                        vm->cursor_visible, vm->interface_enabled, g->verb,
                                        vm->cursor_object == PETKA_NONE ? -1 : (int)vm->cursor_object, g->talking, g->paused,
                                        g->menu_count, g->seq_room, g->map_open, g->case_open, g->star_open, g->save_mode,
                                        hover ? (int)hover->id : -1),
                       (Vector2){10, 98}, 16, 1, LIME);
        }
        // The pointer goes on last, over everything (its z is 1000). It is
        // there whenever the cursor is shown; a dialogue hides it for its
        // phrases and brings it back for a menu.
        // With a controller the star stands for it while it is up, and in a
        // dialogue's menu the lit line does.
        // The save screen is the launcher's own and has it always, whatever
        // the script did with the cursor: after "load" from the start-up
        // menu the script has just been set going, with the cursor off.
        int pointer_shown = !g->extras_open &&
                            (g->save_mode ||
                             (vm->cursor_visible && (g->seq_room == PETKA_NONE || g->map_open) &&
                              (!g->talking || g->menu_count > 0) &&
                              !(plat_pad() && (g->star_open || g->menu_count > 0))));
        if (plat_pad()) {
            if (g->case_open && !g->pad_case_was_open) g->pad_case_return = g->pad_seen;
            if (!g->case_open && g->pad_case_was_open) {
                g->pad_seen = g->pad_case_return;
                if (pointer_shown) {
                    mouse = g->pad_seen;
                    plat_set_pointer(mouse);
                }
            }
            g->pad_case_was_open = g->case_open;
            if (pointer_shown && g->pad_hidden) {
                mouse = g->pad_seen;
                plat_set_pointer(mouse);
            }
            if (pointer_shown) g->pad_seen = mouse;
            g->pad_hidden = !pointer_shown;
        }
        if (g->extras_open) {
            extras_draw(g, font, scale, mouse, debug);
        } else if (pointer_shown) {
            uint16_t clip = g->map_open || g->save_mode ? CURSOR_INTERFACE
                          : send_chapayev || send_walk ? (uint16_t)(CURSOR_ACTION0 + verb)
                          : plat_pad() && vm->cursor_object == PETKA_NONE ? CURSOR_INTERFACE
                          : g->menu_count > 0 ? CURSOR_POINTER
                          : vm->cursor_resource != PETKA_NONE ? vm->cursor_resource
                          : (uint16_t)(CURSOR_ACTION0 + g->verb);
            HideCursor();
            // On the save screen the pointer runs while it is on a slot or
            // a page corner (its mouse function, sub_41E2B0); nothing else
            // there marks what it is over.
            int lively = hover != NULL;
            if (g->save_mode) {
                Vector2 p = {mouse.x / scale, mouse.y / scale};
                lively = CheckCollisionPointRec(p, kSavePrev) || CheckCollisionPointRec(p, kSaveNext);
                for (int i = 0; i < SAVE_SLOTS; i++) lively = lively || CheckCollisionPointRec(p, kSaveSlot[i]);
            }
            // The interface's pointer has its tip at the point, the others
            // their middle.
            draw_cursor(g, clip, lively, clip != CURSOR_INTERFACE, mouse, scale);
        } else {
            ShowCursor();
        }
        plat_end();
        // A play's screenshot is taken two frames on: read right after the
        // buffers are swapped, the screen can still be the frame before.
        if (s_play.shot[0] && ++s_play.shot_wait > 2) {
            Image shot = LoadImageFromScreen();
            ExportImage(shot, s_play.shot);
            UnloadImage(shot);
            s_play.shot[0] = 0;
            s_play.shot_wait = 0;
        }
        if (s_play.over && !s_play.shot[0]) break;
        long series_start = 0, series_step = 1, series_count = 0;
        if (shot_path && getenv("PETKA_SHOT_SERIES") &&
            sscanf(getenv("PETKA_SHOT_SERIES"), "%ld:%ld:%ld", &series_start, &series_step, &series_count) == 3 &&
            series_step > 0 && frame >= series_start && (frame - series_start) % series_step == 0 &&
            (frame - series_start) / series_step < series_count) {
            Image shot = LoadImageFromScreen();
            ExportImage(shot, TextFormat("%s-%ld.png", shot_path, frame));
            UnloadImage(shot);
        }
        if (shot_path && frame >= shot_frame) {
            // Not TakeScreenshot(): it only writes below the working directory.
            Image shot = LoadImageFromScreen();
            ExportImage(shot, shot_path);
            UnloadImage(shot);
            break;
        }
    }

    part_close(g);
    if (g->bg_tex.id) UnloadTexture(g->bg_tex);
    for (int i = 0; i < MAX_SOUNDS; i++) if (g->sounds[i].frameCount > 0) UnloadSound(g->sounds[i]);
    if (font.texture.id != GetFontDefault().texture.id) UnloadFont(font);
    CloseAudioDevice();
    highlight_close();
    plat_close();
    CloseWindow();
    return s_play.failed ? 3 : 0;
}
