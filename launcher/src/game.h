// game.h -- the launcher's shared header: its types, constants and modules.
//
// EN: The launcher is the game's front end, on raylib (a desktop window, or the
// Nintendo Switch through platform.h). The scene is whatever the script VM
// (core, petka/script.h) says it is: each object of the current room is drawn
// from its VM state (resource, visible, z), a click becomes a script message,
// and animations, walks and dialogues report back to the VM when they finish.
// See docs/str-format.md. The launcher is split into modules, one header each,
// all included here; main.c holds the main loop.
//
// RU: Лаунчер - внешняя часть игры на raylib (окно на компьютере или Nintendo
// Switch через platform.h). Сцена - это то, что говорит машина сценария (ядро,
// petka/script.h): каждый объект текущей комнаты рисуется по своему состоянию
// в машине (ресурс, видимость, z), щелчок становится сообщением сценарию, а
// анимации, ходьба и диалоги сообщают машине, когда заканчиваются. Подробности
// в docs/str-format.md. Лаунчер разбит на модули, у каждого свой заголовок,
// все они подключены здесь; в main.c - главный цикл.
//
// Modules / модули:
//   common.c       Общее состояние лаунчера и мелкие помощники: часы игры, строка состояния, поиск объектов.
//   text.c         Текст: перевод CP1251 в UTF-8, кириллический шрифт, перенос строк, обводка, тёмная подложка.
//   resources.c    Файлы игры: архивы (.STR), RESOURCE.QRC, ресурсы по пути, фоны, работа с именами файлов.
//   audio.c        Звук: регуляторы панели, звуки клипов, потоки музыки и фона, звуки на время заставок.
//   visual.c       Клипы на экране: кадры FLIC в текстуры (только та часть, что занимает объект), отрисовка, курсор, часы кадров клипа и проверка попадания.
//   scene.c        Объекты экрана: порядок отрисовки (сортировка по z как в exe) и что находится под точкой.
//   heroes.c       Петька и Чапаев: масштаб по перспективе, клипы ходьбы и шаги по сетке ходьбы, камера.
//   conversation.c Диалоги: выбор диалога, фразы с голосом и субтитрами, меню реплик, описания предметов.
//   host.c         Хост машины скрипта: что лаунчер делает по сообщениям скрипта и при смене комнаты.
//   film_play.c    Видеоролики (.AVI): поиск рядом с игрой и показ на весь экран через источник роликов (film.h).
//   inventory.c    Чемодан (инвентарь): предметы, кнопки, крышка и страницы.
//   pad.c          Помощь контроллеру: притяжение указателя к предметам, обход чемодана по сетке.
//   star.c         Звезда: меню действий, мышью или стиком контроллера, и выполнение действия.
//   screens.c      Карта и панель (меню игры с регуляторами), и файл их настроек.
//   saves.c        Сохранения: экран сохранений, ячейки и картинки, запись и чтение игры.
//   menu.c         Стартовое меню.
//   part.c         Часть игры: открытие и закрытие её архивов и сценария, запуск и смена частей.
//   play.c         Для разработки: прохождение по сценарию (PETKA_PLAY) из списка шагов.
//   scriptfix.c    Исправления сценария игры: его ошибки исправляются при загрузке.
//   restored.c     Восстановленные диалоги: реплики, которые вернуло переиздание Reloaded.
//   highlight.c    Жёлтая обводка вокруг предмета под указателем.
//   extras.c       Меню «Дополнительно» (Tab, на Switch - минус): что добавляет лаунчер, вкл/выкл.
#ifndef PETKA_LAUNCHER_GAME_H
#define PETKA_LAUNCHER_GAME_H

#include "petka/bmp.h"
#include "petka/dialogue.h"
#include "petka/flic.h"
#include "petka/ini.h"
#include "petka/msk.h"
#include "petka/save.h"
#include "petka/script.h"
#include "petka/store.h"
#include "petka/topology.h"
#include "petka/walk.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

#include "crt.h"
#include "film.h"
#include "platform.h"

#define GAME_W 640
#define GAME_H 480
#define MAX_STORES 8
#define MAX_SOUNDS 8
// Scrolling of backgrounds wider than the screen: when Petka comes within
// SCROLL_MARGIN of either side of the screen the view is asked to centre on
// him (the exe's rule and number). The view then moves there 8 pixels a
// frame (the exe's frame function, sub_422140), i.e. SCROLL_SPEED at 60 fps.
#define SCROLL_MARGIN 160.0f
#define SCROLL_SPEED 480.0f // px/s
// A hero's idle or PLAY clip stops on its first frame after one loop: the
// exe (sub_407BF0) winds that clip's clock back by this much.
#define HERO_REST_SECONDS 10.0

// What is on screen for one script object.
typedef struct {
    uint16_t resource; // resource currently loaded, PETKA_NONE = none
    PetkaFlic *flic;
    Texture2D tex;
    PetkaMskRect rect; // bounding rect of the clip inside its frame
    PetkaMskRect area; // the part of the frame the texture holds
    const uint8_t *msk; // the clip's .MSK, NULL if it has none
    size_t msk_len;
    double next_frame;
    int sound_pending; // play the clip's sound when its first frame is up
    int restart;       // rewind before the next frame
    int live;    // drawn this frame
} Visual;

#define SAVE_SLOTS 6
// The menu line under the pointer. The exe's is pure blue (0xFF0000 in its
// BGR), which is hard to read on the dark band behind the menu; a lighter
// blue keeps the look and reads well.
#define BLUE_LINE ((Color){110, 160, 255, 255})
#define NO_CAST_INK ((Color){10, 10, 10, 255}) // a subtitle whose speaker has no CAST.INI colour
#define MAX_WAYPOINTS 32
#define MAX_GAITS 8

// A hero on the move: a route across the room's walk mesh, walked one step
// per frame of the walk clip. Both follow the exe (petka/walk.h).
typedef struct {
    int active;
    PetkaWalker walker;
} Walk;

// The step tables of one set of walk clips, by the path they share
// ("MAIN\WALK\P-" for Petka's W-N.FLC ... W-NW.FLC).
typedef struct {
    char prefix[96];
    PetkaGait *gait; // NULL if the files are missing
} GaitEntry;

typedef struct {
    PetkaStore *stores[MAX_STORES];
    int store_count;
    char *qrc[65536]; // resource id -> store path, from RESOURCE.QRC

    PetkaScript *script;
    PetkaScriptVM *vm;
    PetkaTopology *topo;
    PetkaPhraseTable *phrases;
    PetkaDialogueTree *dtree;
    const uint8_t *bgs_ini; // raw BGS.INI text (CP1251), for the scale params
    size_t bgs_ini_len;

    // Per object, parallel to script->objects, from NAMES.INI and CAST.INI.
    char **names;  // what the player sees it called (CP1251), NULL = unnamed
    Color *colors; // colour of its lines in a dialogue
    char speech_prefix[16];

    Visual *visuals; // parallel to script->objects
    uint16_t *z_order;  // the screen's objects in draw order (z_order_sort())
    int z_order_n;
    uint16_t z_order_room; // the screen they were listed for
    int z_order_heroes;    // ... with the heroes or not
    Visual cursor;   // the pointer's own clip
    Visual carried;  // a thing carried from the case, beside the pointer
    // The case: the game's panel, with the things carried.
    int map_open;    // a room of the script is up as a screen of its own:
                     // the map, or the panel
    int panel_open;  // ... and it is the panel
    int panel_hover; // which of the panel's objects the pointer is on, -1 = none
    int tracks_hushed; // the room's music and ambience are paused for a sequence
    int map_verb;    // the cursor's action to give back when it is left
    float map_camera; // and the view's scroll
    int subtitles;   // the panel's switch: phrases are shown as text
    int boot_load;   // "load" was picked at start-up: no room has been entered yet
    // The star: the menu of actions the right button puts up at the pointer.
    int star_open;
    int star_x, star_y; // its corner on screen
    int star_lit;       // the part under the pointer, 0..5, or 6 for none
    // With a controller (plat_pad) the cursor is the plain pointer: A on an
    // object puts the star up for it, the stick picks a part, A does it.
    // While R is held the cursor sends Chapayev instead, the one action the
    // star has not; while L is held it walks, things or no things.
    uint16_t pad_target; // the object the star is up for
    float pad_x, pad_y;  // and where it was clicked, in the room
    Visual star;
    Texture2D star_grey; // its middle in grey
    // A thing's description (SHOWDESCRIPTION), up until the next click.
    char desc[2048]; // UTF-8, "" = none
    int desc_frame;  // the message's number: a frame of the description clip
    Visual desc_clip;
    // The part of the game that is loaded, and the way to another.
    char game_dir[1024];
    char part[16];      // "PART_n", the folder its files are under
    int part_number;
    int game;           // 1: the first game; 2: the second («Петька 2: Судный
                        // день»), told by its stores (part_open)
    int chapter;
    int chapter_store;  // which of `stores` is the chapter's, -1 = none
    PetkaScriptHost host;
    int switch_part, switch_chapter; // load this part at the top of the next
                                     // frame; 0 = stay
    char pending_load[64];           // ... and then this saved game
    // Saved games.
    int save_mode;   // the save screen is up: 1 loading, 2 saving
    int save_page;   // 0..2, six slots a page
    Texture2D save_bg;
    struct { int present; Texture2D tex; char label[96]; } save_slot[SAVE_SLOTS];
    uint8_t thumb[PETKA_SAVE_THUMB_BYTES]; // the room as it was when the panel came up
    // The panel's four dials, as the exe keeps them ("Options" in its
    // profile): the number of the dial clip's frame, from 1.
    int dial[4];     // speech 1..31, music 1..41, effects 1..31, speed 1..26
    int case_open;
    int case_first;  // number of the first thing shown, a page is six
    int case_lit;    // the button under the pointer, -1 = none
    Visual case_body, case_exit, case_strip, case_button[6];
    // With a controller the star's middle opens the case to pick a thing
    // for the object the star was up for: a thing clicked is used on it at
    // once. There the lid's Chapayev, diskette and map do nothing and are
    // grey.
    uint16_t case_for;  // that object, PETKA_NONE = the case as usual
    // With a controller the pointer in the case goes from thing to thing
    // only; L and R turn the pages, B shuts it.
    int case_focus;     // the place on the page (0..5) the pointer is on
    // The extras menu (Tab, or - on the Switch) and what it switches.
    int extras_open;
    int extras_focus;
    int debug;          // the F1 overlay (and the extras' row for it)
    Font font;          // the launcher's text font, for screens of their own
    int opt_aim;        // the pointer is drawn to things nearby (a controller)
    int opt_highlight;  // a yellow outline round the things that can be clicked
    int opt_restored;   // the restored dialogues (restored.c) are said
    // The player's last action, until a reaction to it asks for a restored
    // line (restored_player_action): PETKA_NONE = none waiting.
    uint16_t acted_obj, acted_op;
    // Phrases a restored line has said after its dialogue, before the
    // reaction (restored.c's `then`), and how many are said yet.
    uint16_t then_phrase[4];
    int then_count, then_pos;
    // A split restored dialogue (restored.h): phrases still to say in its
    // first part (-1: no limit), and phrases to pass over in its second.
    int say_limit, say_skip;
    int phrases_only; // a RESTORED_PHRASES: then_phrase alone, no dialogue
    // The lid's pictures in grey (case_lid_grey()): [0] all three, for the
    // case that picks a thing for an object; [1] Chapayev's alone -- a
    // controller sends him with R, and his button is dead in any case.
    Texture2D case_lid_grey[2];
    PetkaMskRect case_lid_span; // where on the case those textures go
    Texture2D bg_tex;
    float camera_x;         // left edge of the view, in background pixels
    float camera_target;    // where the view has been asked to move to
    int camera_snap;        // a room was just entered: centre the view, don't glide
    PetkaWalkMesh *mesh;    // the current background's floor, NULL if it has none
    uint16_t mesh_resource; // background the mesh belongs to
    uint16_t bg_resource;
    uint16_t shown_room;
    // A cutscene "sequence" (SETSEQ): this room is on screen instead of the
    // one the game is in, until ENDSEQ. PETKA_NONE = no sequence.
    uint16_t seq_room;

    // Hero scale by screen row, from BGS.INI; see hero_scale().
    float persp_base, persp_delta, persp_y0, persp_y1, persp_max;
    Walk walk[2];
    GaitEntry gaits[MAX_GAITS];
    int gait_count;

    const PetkaDialogueVerb *dialogue; // picked by the script, played by DIALOG
    PetkaDialogueVM dvm;
    int talking;
    int paused;        // dialogue waits for the script's CONTINUE
    uint16_t talker;   // object speaking the current phrase, PETKA_NONE = nobody
    uint32_t menu[16]; // phrase indices of the lines the player may pick
    int menu_count;    // > 0 while the dialogue waits for that pick
    // With a controller (plat_pad) the pointer is held from a dialogue's
    // first phrase to its end, and then goes back to where it was; while a
    // menu is up it sits on the line picked out by up and down.
    int menu_focus;    // that line
    int pad_hold;      // the pointer is held
    // With a controller, whenever the pointer is not drawn -- a dialogue, a
    // sequence, the star, the extras, a cursor the script has hidden -- it
    // is held, and drawn again it is back where it was last seen.
    int pad_hidden;     // it was not drawn last frame
    Vector2 pad_seen;   // where it was last drawn
    // The case takes the pointer to its things; shut, it gives it back
    // where it was before -- on the object the star was up for, if that is
    // how the case was opened.
    int pad_case_was_open;
    Vector2 pad_case_return;
    int auto_pick;     // dev: line to pick without asking; -1 = the last one, -2 = ask
    int dialogue_done; // tell the VM on the next frame
    char line[512];    // current phrase, UTF-8
    double line_until;
    Sound voice;

    Music music, ambience;
    uint16_t music_id, ambience_id;
    // A sequence's own tracks, apart from the room's (the sequence screen
    // keeps what it last played, +36 and +40).
    Music seq_music, seq_ambience;
    uint16_t seq_music_id, seq_ambience_id;
    int seq_tracks_paused;
    Sound sounds[MAX_SOUNDS];
    const uint8_t *sound_src[MAX_SOUNDS]; // which file each slot holds
    unsigned char sound_paused[MAX_SOUNDS]; // stopped for a sequence, to go on after it
    int voice_paused;

    int verb;
    int quit;
    int to_menu;      // the game is over: back to the start-up menu
    int map_request;  // TOMAP: the map command, at the top of the next frame
    int menu_on_back; // the save screen came from that menu: leaving it goes back there
    int fast;  // dev: no voice, phrases flash by
    int trace; // dev: log delivered script messages
    int clips_quiet; // clips are being run out of sight: no sounds
    char status[256];
    double status_until;
} Game;
// EN: One of the cursor's actions: the script message it sends, and its name.
// RU: Одно из действий курсора: сообщение, которое оно шлёт сценарию, и его название.
typedef struct {
    uint16_t op;
    const char *label;
} Verb;
#define VERB_COUNT ((int)(sizeof(kVerbs) / sizeof(kVerbs[0])))

// The modules / модули
#include "common.h"
#include "text.h"
#include "resources.h"
#include "audio.h"
#include "visual.h"
#include "scene.h"
#include "heroes.h"
#include "conversation.h"
#include "host.h"
#include "film_play.h"
#include "inventory.h"
#include "pad.h"
#include "star.h"
#include "screens.h"
#include "saves.h"
#include "menu.h"
#include "part.h"
#include "play.h"
#include "extras.h"
#include "highlight.h"
#include "restored.h"
#include "scriptfix.h"

#endif
