// common.c
//
// EN: The launcher's shared state and small helpers: the game's clock, the status line, object lookups.
// RU: Общее состояние лаунчера и мелкие помощники: часы игры, строка состояния, поиск объектов.
#include "game.h"

// The cursor's actions. A click on an object posts the action's message to
// it (the exe's click handler, sub_401B30): the sender is the object itself
// and the arguments are zero, except that WALK carries the clicked point.
// The last action sends Chapayev over: OBJECTUSE with him as the sender and
// the clicked point. (A seventh, using an inventory item, is OBJECTUSE with
// the item as the sender; it is picked from the inventory, not from here.)
// They are in the exe's own order -- the index is the cursor's action number
// (+128), and its clip is resource 5002 + action -- and, like the exe's
// cursor (sub_402280), the game starts on the first one, "look".
const Verb kVerbs[6] = {
    {PETKA_OP_LOOK, "СМОТРЕТЬ"}, {PETKA_OP_WALK, "ИДТИ"}, {PETKA_OP_USE, "ПРИМЕНИТЬ"},
    {PETKA_OP_TAKE, "ВЗЯТЬ"}, {PETKA_OP_TALK, "ГОВОРИТЬ"}, {PETKA_OP_OBJECTUSE, "ЧАПАЕВ"},
};

// The game's clock. Normally the real one; an unattended run can ask for a
// fixed 60 steps a second with no waiting in between (PETKA_TURBO), so that
// minutes of cutscene go by in seconds and runs repeat exactly.
double s_clock;
int s_turbo;
double game_now(void) { return s_turbo ? s_clock : GetTime(); }
float game_dt(void) { return s_turbo ? 1.0f / 60.0f : GetFrameTime(); }

void set_status(Game *g, const char *text) {
    snprintf(g->status, sizeof(g->status), "%s", text);
    g->status_until = game_now() + 4.0;
}

int obj_index(const Game *g, const PetkaObject *obj) {
    return (int)(obj - g->script->objects);
}

int is_hero(uint16_t id) { return id == PETKA_ID_PETKA || id == PETKA_ID_CHAPAYEV; }

// The room on screen: the sequence being played, or else the room the game
// is in. A sequence is a room made of full-screen clips, usually on the
// black MAIN\BG1.BMP; the script moves it along with the clips' END
// reactions and closes it with ENDSEQ. No INITBG/LEAVEBG is involved, and
// the game's own room can change (GOTO) while the sequence is up.
uint16_t screen_room(const Game *g) {
    return g->seq_room != PETKA_NONE ? g->seq_room : g->vm->room;
}
