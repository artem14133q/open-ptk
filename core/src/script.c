#include "petka/script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- opcode names ----

static const char *const kOpcodeNames[] = {
    [0] = "QM_ZERO", [1] = "USE", [2] = "QM_SETPOS", [3] = "GOTO", [4] = "LOOK",
    [5] = "SAY", [6] = "TAKE", [7] = "QM_USE", [8] = "QM_VASILYIVANOVICH",
    [9] = "WALK", [10] = "TALK", [11] = "END", [12] = "QM_SETANIMATIONID",
    [13] = "QM_FORCEMOVE", [14] = "SET", [15] = "SHOW", [16] = "HIDE",
    [17] = "DIALOG", [18] = "ZBUFFER", [19] = "TOTALINIT", [20] = "ANIMATE",
    [21] = "STATUS", [22] = "ADDINV", [23] = "DELINV", [24] = "STOP",
    [25] = "CURSOR", [26] = "OBJECTUSE", [27] = "ACTIVE", [28] = "SAID",
    [29] = "SETSEQ", [30] = "ENDSEQ", [31] = "CHECK", [32] = "IF",
    [33] = "SHOWDESCRIPTION", [34] = "HALF", [35] = "QM_WALKED", [36] = "WALKTO",
    [37] = "WALKVICH", [38] = "INITBG", [39] = "USERMSG", [40] = "SYSTEM",
    [41] = "QM_RESERVED1", [42] = "CONTINUE", [43] = "MAP", [44] = "PASSIVE",
    [45] = "NOMAP", [46] = "SETINV", [47] = "BGSFX", [48] = "MUSIC",
    [49] = "IMAGE", [50] = "STAND", [51] = "ON", [52] = "OFF", [53] = "PLAY",
    [54] = "LEAVEBG", [57] = "RANDOM", [58] = "JUMP", [59] = "JUMPVICH",
    [60] = "PART", [61] = "CHAPTER", [62] = "AVI",
};

const char *petka_opcode_name(uint16_t opcode) {
    static char buf[16];
    if (opcode < sizeof(kOpcodeNames) / sizeof(kOpcodeNames[0]) && kOpcodeNames[opcode])
        return kOpcodeNames[opcode];
    snprintf(buf, sizeof(buf), "OP_%u", opcode);
    return buf;
}

// ---- SCRIPT.DAT + BACKGRND.BG ----

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    int error;
} Reader;

static uint32_t rd(Reader *r, size_t n) {
    if (r->error || r->len - r->pos < n) { r->error = 1; return 0; }
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) v |= (uint32_t)r->data[r->pos + i] << (8 * i);
    r->pos += n;
    return v;
}

// Guards calloc() against counts read from a corrupt file: every record is
// at least `min_record` bytes, so a valid count can't exceed what's left.
static int count_ok(Reader *r, uint32_t count, size_t min_record) {
    if (r->error || (r->len - r->pos) / min_record < count) { r->error = 1; return 0; }
    return 1;
}

static void parse_object(Reader *r, PetkaObject *obj) {
    obj->id = (uint16_t)rd(r, 2);
    uint32_t name_len = rd(r, 4);
    if (!count_ok(r, name_len, 1)) return;
    obj->name = (char *)malloc((size_t)name_len + 1);
    memcpy(obj->name, r->data + r->pos, name_len);
    obj->name[name_len] = 0;
    r->pos += name_len;

    uint32_t reaction_count = rd(r, 4);
    if (!count_ok(r, reaction_count, 9)) return;
    obj->reactions = (PetkaReaction *)calloc(reaction_count ? reaction_count : 1, sizeof(PetkaReaction));
    obj->reaction_count = reaction_count;
    for (uint32_t i = 0; i < reaction_count; i++) {
        PetkaReaction *re = &obj->reactions[i];
        re->opcode = (uint16_t)rd(r, 2);
        re->status = (uint8_t)rd(r, 1);
        re->sender = (uint16_t)rd(r, 2);
        uint32_t n = rd(r, 4);
        if (!count_ok(r, n, 10)) return;
        re->messages = (PetkaMessage *)calloc(n ? n : 1, sizeof(PetkaMessage));
        re->message_count = n;
        for (uint32_t k = 0; k < n; k++) {
            PetkaMessage *m = &re->messages[k];
            m->obj_id = (uint16_t)rd(r, 2);
            m->opcode = (uint16_t)rd(r, 2);
            m->arg1 = (uint16_t)rd(r, 2);
            m->arg2 = (uint16_t)rd(r, 2);
            m->arg3 = (uint16_t)rd(r, 2);
        }
    }
}

static int parse_backgrounds(PetkaScript *script, const uint8_t *data, size_t len) {
    Reader r = { data, len, 0, 0 };
    uint32_t room_count = rd(&r, 4);
    if (!count_ok(&r, room_count, 6)) return 0;
    for (uint32_t i = 0; i < room_count; i++) {
        uint16_t room_id = (uint16_t)rd(&r, 2);
        uint32_t n = rd(&r, 4);
        if (!count_ok(&r, n, 22)) return 0;
        PetkaObject *room = petka_script_find(script, room_id);
        // A room listed twice would leak the first list; reject instead.
        if (!room || !room->is_room || room->attached) return 0;
        room->attached = (PetkaAttach *)calloc(n ? n : 1, sizeof(PetkaAttach));
        room->attached_count = n;
        for (uint32_t k = 0; k < n; k++) {
            PetkaAttach *a = &room->attached[k];
            a->obj_id = (uint16_t)rd(&r, 2);
            a->x = (int32_t)rd(&r, 4);
            a->y = (int32_t)rd(&r, 4);
            a->z = (int32_t)rd(&r, 4);
            a->walk_x = (int32_t)rd(&r, 4);
            a->walk_y = (int32_t)rd(&r, 4);
        }
    }
    return !r.error && r.pos == r.len;
}

PetkaScript *petka_script_open(const uint8_t *script_data, size_t script_len,
                               const uint8_t *bg_data, size_t bg_len) {
    Reader r = { script_data, script_len, 0, 0 };
    uint32_t object_count = rd(&r, 4);
    uint32_t room_count = rd(&r, 4);
    // Smallest object record: id(2) + name length(4) + reaction count(4).
    if (!count_ok(&r, object_count, 10) || !count_ok(&r, room_count, 10)) return NULL;
    uint32_t total = object_count + room_count;
    if (!count_ok(&r, total, 10)) return NULL;

    PetkaScript *script = (PetkaScript *)calloc(1, sizeof(PetkaScript));
    script->objects = (PetkaObject *)calloc(total ? total : 1, sizeof(PetkaObject));
    script->object_count = total;
    script->room_count = room_count;
    for (uint32_t i = 0; i < total && !r.error; i++) {
        script->objects[i].is_room = i >= object_count;
        parse_object(&r, &script->objects[i]);
    }

    if (r.error || r.pos != r.len ||
        (bg_data && !parse_backgrounds(script, bg_data, bg_len))) {
        petka_script_close(script);
        return NULL;
    }
    return script;
}

void petka_script_close(PetkaScript *script) {
    if (!script) return;
    for (uint32_t i = 0; i < script->object_count; i++) {
        PetkaObject *obj = &script->objects[i];
        for (uint32_t k = 0; k < obj->reaction_count; k++) free(obj->reactions[k].messages);
        free(obj->reactions);
        free(obj->attached);
        free(obj->name);
    }
    free(script->objects);
    free(script);
}

PetkaObject *petka_script_find(const PetkaScript *script, uint16_t id) {
    for (uint32_t i = 0; i < script->object_count; i++)
        if (script->objects[i].id == id) return &script->objects[i];
    return NULL;
}

PetkaObject *petka_script_find_name(const PetkaScript *script, const char *name) {
    for (uint32_t i = 0; i < script->object_count; i++)
        if (script->objects[i].name && strcmp(script->objects[i].name, name) == 0)
            return &script->objects[i];
    return NULL;
}

// ---- message VM ----

// A script that never waits (no PLAY, DIALOG, walk or busy object) would
// keep petka_scriptvm_run() spinning forever; give up after this many
// deliveries in a single call.
#define RUN_LIMIT 200000

static int is_hero(uint16_t id) { return id == PETKA_ID_PETKA || id == PETKA_ID_CHAPAYEV; }

static PetkaObjectState *state_of(PetkaScriptVM *vm, const PetkaObject *obj) {
    return &vm->state[obj - vm->script->objects];
}

PetkaObjectState *petka_scriptvm_state(PetkaScriptVM *vm, uint16_t id) {
    PetkaObject *obj = petka_script_find(vm->script, id);
    return obj ? state_of(vm, obj) : NULL;
}

static void tail_clear(PetkaTail *tail) {
    free(tail->messages);
    tail->messages = NULL;
    tail->count = 0;
    tail->present = 0;
}

// Replaces `tail` with a copy of msgs[from..count).
static void tail_set(PetkaTail *tail, const PetkaMessage *msgs, uint32_t count, uint32_t from) {
    uint32_t n = from < count ? count - from : 0;
    PetkaMessage *copy = (PetkaMessage *)malloc((n ? n : 1) * sizeof(PetkaMessage));
    if (n) memcpy(copy, msgs + from, n * sizeof(PetkaMessage));
    tail_clear(tail);
    tail->messages = copy;
    tail->count = n;
    tail->present = 1;
}

PetkaScriptVM *petka_scriptvm_open(PetkaScript *script, const PetkaScriptHost *host) {
    PetkaScriptVM *vm = (PetkaScriptVM *)calloc(1, sizeof(PetkaScriptVM));
    if (!vm) return NULL;
    vm->script = script;
    vm->state = (PetkaObjectState *)calloc(script->object_count ? script->object_count : 1,
                                           sizeof(PetkaObjectState));
    if (!vm->state) { free(vm); return NULL; }
    if (host) vm->host = *host;
    vm->room = PETKA_NONE;
    vm->dialog_sender = -1;
    vm->interface_enabled = 1;
    vm->cursor_visible = 1;
    vm->cursor_resource = PETKA_NONE;
    vm->cursor_object = PETKA_NONE;
    vm->rng = 0x12345678u;

    for (uint32_t i = 0; i < script->object_count; i++) {
        PetkaObjectState *st = &vm->state[i];
        // Objects start switched on; the ones that should be hidden turn
        // themselves OFF in their TOTALINIT reaction.
        st->visible = 1;
        st->active = 1;
        st->repeat_sound = 1;
        // The script only ever freezes objects (ANIMATE 0 is 5x as common
        // as ANIMATE 1), so they start out animated.
        st->animate = 1;
        st->resource = PETKA_NONE;
        st->play_wait = PETKA_NONE;
        st->z = -1;
        st->walk_x = st->walk_y = -1;
        st->room = PETKA_NONE;
        st->bgsfx = st->music = PETKA_NONE;
        st->costume = PETKA_NONE;
        st->walk_sender = -1;
    }
    for (uint32_t i = 0; i < script->object_count; i++) {
        const PetkaObject *room = &script->objects[i];
        for (uint32_t k = 0; k < room->attached_count; k++) {
            const PetkaAttach *a = &room->attached[k];
            PetkaObjectState *st = petka_scriptvm_state(vm, a->obj_id);
            if (!st) continue;
            st->room = room->id;
            st->x = a->x;
            st->y = a->y;
            st->z = a->z;
            st->walk_x = a->walk_x;
            st->walk_y = a->walk_y;
        }
    }
    return vm;
}

void petka_scriptvm_close(PetkaScriptVM *vm) {
    if (!vm) return;
    for (uint32_t i = 0; i < vm->script->object_count; i++) {
        tail_clear(&vm->state[i].play_tail);
        tail_clear(&vm->state[i].walk_tail);
    }
    tail_clear(&vm->dialog_tail);
    free(vm->state);
    free(vm->queue);
    free(vm->inventory);
    free(vm);
}

static void enqueue(PetkaScriptVM *vm, const PetkaMessage *msg, int sender) {
    if (vm->queue_count == vm->queue_cap) {
        vm->queue_cap = vm->queue_cap ? vm->queue_cap * 2 : 64;
        vm->queue = (PetkaQueued *)realloc(vm->queue, vm->queue_cap * sizeof(PetkaQueued));
    }
    vm->queue[vm->queue_count].msg = *msg;
    vm->queue[vm->queue_count].sender = sender;
    vm->queue_count++;
}

void petka_scriptvm_post(PetkaScriptVM *vm, uint16_t obj_id, uint16_t opcode,
                         uint16_t arg1, uint16_t arg2, uint16_t arg3, int sender) {
    PetkaMessage m = { obj_id, opcode, arg1, arg2, arg3 };
    enqueue(vm, &m, sender);
}

void petka_scriptvm_broadcast(PetkaScriptVM *vm, uint16_t opcode, uint16_t arg1,
                              uint16_t arg2, uint16_t arg3, int sender) {
    for (uint32_t i = 0; i < vm->script->object_count; i++)
        petka_scriptvm_post(vm, vm->script->objects[i].id, opcode, arg1, arg2, arg3, sender);
}

static int has_dialogue(PetkaScriptVM *vm, uint16_t obj_id, uint32_t key) {
    return vm->host.has_dialogue && vm->host.has_dialogue(vm->host.user, obj_id, key);
}

static void walk_hero(PetkaScriptVM *vm, uint16_t hero_id, int32_t x, int32_t y) {
    PetkaObjectState *hero = petka_scriptvm_state(vm, hero_id);
    if (!hero) return;
    if (vm->host.walk && vm->host.walk(vm->host.user, hero_id, x, y)) {
        hero->busy = 1;
    } else {
        // No walk happened: the hero is simply there.
        hero->x = x;
        hero->y = y;
    }
}

// Target point of a walk/jump message: its own arguments, or the object's
// walk point when either is missing. Returns 0 if there is no point at all.
static int walk_target(const PetkaObjectState *st, const PetkaMessage *m, int32_t *x, int32_t *y) {
    if (m->arg1 != PETKA_NONE && m->arg2 != PETKA_NONE) {
        *x = (int16_t)m->arg1;
        *y = (int16_t)m->arg2;
        return 1;
    }
    *x = st->walk_x;
    *y = st->walk_y;
    return *x != -1;
}

// The rest of a reaction is parked on the hero until he has walked: a
// WALKED message is queued right behind the walk message and stays there
// while the hero is busy (sub_408080).
static void hero_wait_walk(PetkaScriptVM *vm, uint16_t hero_id, const PetkaMessage *msgs,
                           uint32_t count, uint32_t index, int sender) {
    PetkaObjectState *hero = petka_scriptvm_state(vm, hero_id);
    if (!hero) return;
    // Whatever he was walking for before is off.
    petka_scriptvm_stop_hero(vm, hero_id);
    petka_scriptvm_post(vm, hero_id, PETKA_OP_WALKED, 0, 0, 0, hero_id);
    tail_set(&hero->walk_tail, msgs, count, index + 1);
    hero->walk_sender = sender;
}

// Posts msgs[index] and decides whether the reaction goes on. Returns 1 to
// continue with the next message, 0 if the rest was parked or cut off.
static int post_step(PetkaScriptVM *vm, const PetkaMessage *msgs, uint32_t count,
                     uint32_t index, int sender) {
    const PetkaMessage *m = &msgs[index];
    enqueue(vm, m, sender);
    switch (m->opcode) {
    case PETKA_OP_DIALOG:
        tail_set(&vm->dialog_tail, msgs, count, index + 1);
        return 0;
    case PETKA_OP_PLAY: {
        PetkaObjectState *target = petka_scriptvm_state(vm, m->obj_id);
        if (target) {
            tail_set(&target->play_tail, msgs, count, index + 1);
            target->play_wait = m->arg1;
        }
        return 0;
    }
    case PETKA_OP_WALK:
    case PETKA_OP_WALKTO:
        hero_wait_walk(vm, PETKA_ID_PETKA, msgs, count, index, sender);
        return 0;
    case PETKA_OP_WALKVICH:
        hero_wait_walk(vm, PETKA_ID_CHAPAYEV, msgs, count, index, sender);
        return 0;
    default:
        return 1;
    }
}

// CHECK cuts a reaction off unless its target object has the given status.
// It is evaluated when the reaction is posted, so a STATUS earlier in the
// same reaction has not been applied yet.
static int check_fails(PetkaScriptVM *vm, const PetkaMessage *m) {
    if (m->opcode != PETKA_OP_CHECK) return 0;
    PetkaObjectState *target = petka_scriptvm_state(vm, m->obj_id);
    return !target || target->status != m->arg1;
}

// Continues a parked reaction (sub_404790). Unlike the first pass in
// process_base(), IF and RANDOM are not evaluated here.
static void run_tail(PetkaScriptVM *vm, PetkaTail *slot, int sender) {
    if (!slot->present) return;
    // Detach first: post_step() may park a new tail in this very slot.
    PetkaTail cur = *slot;
    slot->messages = NULL;
    slot->count = 0;
    slot->present = 0;
    for (uint32_t i = 0; i < cur.count; i++) {
        if (check_fails(vm, &cur.messages[i])) break;
        if (!post_step(vm, cur.messages, cur.count, i, sender)) break;
    }
    free(cur.messages);
}

static void inventory_add(PetkaScriptVM *vm, uint16_t id) {
    if (vm->inventory_count == vm->inventory_cap) {
        vm->inventory_cap = vm->inventory_cap ? vm->inventory_cap * 2 : 16;
        vm->inventory = (uint16_t *)realloc(vm->inventory, vm->inventory_cap * sizeof(uint16_t));
    }
    vm->inventory[vm->inventory_count++] = id;
}

static void inventory_remove(PetkaScriptVM *vm, uint16_t id) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < vm->inventory_count; i++)
        if (vm->inventory[i] != id) vm->inventory[n++] = vm->inventory[i];
    vm->inventory_count = n;
}

int petka_scriptvm_has_item(const PetkaScriptVM *vm, uint16_t obj_id) {
    for (uint32_t i = 0; i < vm->inventory_count; i++)
        if (vm->inventory[i] == obj_id) return 1;
    return 0;
}

// The built-in effect of a message, shared by every object class
// (the second half of sub_403270).
static void apply_builtin(PetkaScriptVM *vm, PetkaObject *obj, const PetkaQueued *q, int reacted) {
    PetkaObjectState *st = state_of(vm, obj);
    const PetkaMessage *m = &q->msg;
    int32_t x, y;

    switch (m->opcode) {
    case PETKA_OP_SETPOS:
        st->x = (int16_t)m->arg1;
        st->y = (int16_t)m->arg2;
        break;
    case PETKA_OP_WALK:
        // A click with no scripted reaction: Petka walks to the object, or
        // to the clicked point when the object has no walk point.
        if (!reacted) {
            if (st->walk_x != -1) walk_hero(vm, PETKA_ID_PETKA, st->walk_x, st->walk_y);
            else walk_hero(vm, PETKA_ID_PETKA, (int16_t)m->arg1, (int16_t)m->arg2);
        }
        break;
    case PETKA_OP_END:
        if (st->play_tail.present && st->play_wait == m->arg1)
            run_tail(vm, &st->play_tail, obj->id);
        break;
    case PETKA_OP_SET:
    case PETKA_OP_PLAY:
        st->resource = m->arg1;
        if (!obj->is_room) st->repeat_sound = m->arg2 != 5;
        break;
    case PETKA_OP_SHOW:
        st->visible = m->arg1 != 0;
        break;
    case PETKA_OP_HIDE:
        st->visible = 0;
        break;
    case PETKA_OP_DIALOG:
        vm->dialog_sender = obj->id;
        if (vm->host.start_dialogue) vm->host.start_dialogue(vm->host.user, obj->id, m->arg1);
        break;
    case PETKA_OP_ZBUFFER:
        st->zbuffer = m->arg1;
        if (m->arg2 != PETKA_NONE) st->z = m->arg2;
        break;
    case PETKA_OP_ANIMATE:
        st->animate = m->arg1;
        break;
    case PETKA_OP_STATUS:
        st->status = (uint8_t)m->arg1;
        break;
    case PETKA_OP_ADDINV:
        inventory_add(vm, obj->id);
        break;
    case PETKA_OP_DELINV:
        inventory_remove(vm, obj->id);
        break;
    case PETKA_OP_STOP:
        // Shows/hides the cursor and switches the panel with it.
        vm->cursor_visible = m->arg1 != 0;
        vm->interface_enabled = m->arg1 != 0;
        break;
    case PETKA_OP_CURSOR:
        if (m->arg1 == PETKA_NONE) {
            vm->cursor_resource = PETKA_NONE;
            vm->cursor_object = PETKA_NONE;
        } else {
            // The cursor now carries this inventory object.
            vm->cursor_resource = m->arg1;
            vm->cursor_object = obj->id;
            st->visible = 0;
            st->active = 0;
        }
        break;
    case PETKA_OP_OBJECTUSE:
        // Using Chapayev on an object with no scripted reaction sends him
        // over to it.
        if (q->sender == (int)PETKA_ID_CHAPAYEV && !reacted) {
            if (st->walk_x != -1) walk_hero(vm, PETKA_ID_CHAPAYEV, st->walk_x, st->walk_y);
            else walk_hero(vm, PETKA_ID_CHAPAYEV, (int16_t)m->arg1, (int16_t)m->arg2);
        }
        break;
    case PETKA_OP_ACTIVE:
        st->active = m->arg1;
        break;
    case PETKA_OP_PASSIVE:
        st->active = 0;
        break;
    case PETKA_OP_WALKTO:
        if (walk_target(st, m, &x, &y)) {
            walk_hero(vm, PETKA_ID_PETKA, x, y);
            // Chapayev follows when Petka is about to leave the room.
            PetkaObjectState *petka = petka_scriptvm_state(vm, PETKA_ID_PETKA);
            if (petka && petka->walk_tail.present) {
                for (uint32_t i = 0; i < petka->walk_tail.count; i++) {
                    if (petka->walk_tail.messages[i].opcode == PETKA_OP_GOTO) {
                        walk_hero(vm, PETKA_ID_CHAPAYEV, x, y);
                        break;
                    }
                }
            }
        }
        break;
    case PETKA_OP_WALKVICH:
        if (walk_target(st, m, &x, &y)) walk_hero(vm, PETKA_ID_CHAPAYEV, x, y);
        break;
    case PETKA_OP_SYSTEM:
        if (m->arg1 == 0) vm->interface_enabled = 0;
        else if (m->arg1 == 1) vm->interface_enabled = 1;
        // 242 quits the game; left to the host's message callback.
        break;
    case PETKA_OP_SETINV:
        // This object takes the sender's place in the inventory.
        for (uint32_t i = 0; i < vm->inventory_count; i++)
            if ((int)vm->inventory[i] == q->sender) vm->inventory[i] = obj->id;
        break;
    case PETKA_OP_ON:
        st->active = 1;
        st->visible = 1;
        break;
    case PETKA_OP_OFF:
        st->active = 0;
        st->visible = 0;
        break;
    case PETKA_OP_JUMP:
    case PETKA_OP_JUMPVICH:
        if (walk_target(st, m, &x, &y)) {
            PetkaObjectState *hero = petka_scriptvm_state(
                vm, m->opcode == PETKA_OP_JUMP ? PETKA_ID_PETKA : PETKA_ID_CHAPAYEV);
            if (hero) { hero->x = x; hero->y = y; }
        }
        break;
    default:
        break;
    }
}

// Picks the reactions of `obj` that match the message, posts their
// messages and applies the message's built-in effect (sub_403270).
// Returns whether a reaction (or a dialogue) handled the message.
static int process_base(PetkaScriptVM *vm, PetkaObject *obj, const PetkaQueued *q) {
    PetkaObjectState *st = state_of(vm, obj);
    const PetkaMessage *in = &q->msg;
    int reacted = 0;

    uint32_t key = in->opcode;
    if (in->opcode == PETKA_OP_OBJECTUSE && q->sender >= 0)
        key = ((uint32_t)q->sender << 16) | PETKA_OP_OBJECTUSE;

    for (uint32_t r = 0; r < obj->reaction_count; r++) {
        PetkaReaction *re = &obj->reactions[r];
        if (re->opcode != in->opcode) continue;
        if (re->status != 0xFF && re->status != st->status) continue;
        if (re->sender != PETKA_NONE && (int)re->sender != q->sender) continue;

        if (has_dialogue(vm, obj->id, key) && vm->host.select_dialogue)
            vm->host.select_dialogue(vm->host.user, obj->id, key);

        for (uint32_t i = 0; i < re->message_count; i++) {
            PetkaMessage *m = &re->messages[i];
            if (check_fails(vm, m)) break;
            // IF compares its arguments with those of the incoming message.
            if (m->opcode == PETKA_OP_IF &&
                ((m->arg1 != PETKA_NONE && m->arg1 != in->arg1) ||
                 (m->arg2 != PETKA_NONE && m->arg2 != in->arg2) ||
                 (m->arg3 != PETKA_NONE && m->arg3 != in->arg3)))
                break;
            // RANDOM stores its roll in the script itself, as the exe does.
            if (m->opcode == PETKA_OP_RANDOM && m->arg2 != PETKA_NONE && m->arg2 != 0) {
                vm->rng = vm->rng * 1103515245u + 12345u;
                m->arg1 = (uint16_t)(((vm->rng >> 16) & 0x7FFF) % m->arg2);
            }
            if (!post_step(vm, re->messages, re->message_count, i, obj->id)) break;
        }
        reacted = 1;
    }

    if (!reacted && has_dialogue(vm, obj->id, key)) {
        // No scripted reaction, but DIALOGUE.FIX has a line for this verb.
        if (vm->host.select_dialogue) vm->host.select_dialogue(vm->host.user, obj->id, key);
        vm->dialog_sender = obj->id;
        tail_clear(&vm->dialog_tail);
        if (vm->host.start_dialogue) vm->host.start_dialogue(vm->host.user, obj->id, in->arg1);
        return 1;
    }

    apply_builtin(vm, obj, q, reacted);
    return reacted;
}

void petka_scriptvm_enter_room(PetkaScriptVM *vm, uint16_t room, int silent) {
    if (vm->room == room) return;
    uint16_t prev = vm->room;
    if (prev != PETKA_NONE && !silent)
        petka_scriptvm_broadcast(vm, PETKA_OP_LEAVEBG, 0, 0, 0, prev);
    vm->room = room;
    if (vm->host.room_changed) vm->host.room_changed(vm->host.user, prev, room);
    if (!silent) petka_scriptvm_broadcast(vm, PETKA_OP_INITBG, 0, 0, 0, room);
}

// Rooms (sub_401F80).
static int process_room(PetkaScriptVM *vm, PetkaObject *obj, const PetkaQueued *q) {
    int reacted = process_base(vm, obj, q);
    PetkaObjectState *st = state_of(vm, obj);
    switch (q->msg.opcode) {
    case PETKA_OP_GOTO:
        petka_scriptvm_stop_hero(vm, PETKA_ID_PETKA);
        petka_scriptvm_stop_hero(vm, PETKA_ID_CHAPAYEV);
        petka_scriptvm_enter_room(vm, obj->id, 0);
        break;
    case PETKA_OP_MAP:
        st->on_map = q->msg.arg1 != 0;
        break;
    case PETKA_OP_NOMAP:
        st->on_map = 0;
        break;
    case PETKA_OP_BGSFX:
        st->bgsfx = q->msg.arg1;
        break;
    case PETKA_OP_MUSIC:
        st->music = q->msg.arg1;
        break;
    default:
        break;
    }
    return reacted;
}

// Petka and Chapayev (sub_407EA0): several opcodes are rewritten into a SET
// of the matching costume animation before the common handling.
static int process_hero(PetkaScriptVM *vm, PetkaObject *obj, PetkaQueued *q) {
    PetkaObjectState *st = state_of(vm, obj);
    PetkaMessage *m = &q->msg;

    if (m->opcode == PETKA_OP_IMAGE) {
        m->opcode = PETKA_OP_SET;
        st->costume = m->arg1;
    }
    if (m->opcode == PETKA_OP_SAID || m->opcode == PETKA_OP_STAND) {
        m->opcode = PETKA_OP_SET;
        m->arg1 = st->costume;
        m->arg2 = 1;
    }
    if (m->opcode == PETKA_OP_SAY) {
        m->opcode = PETKA_OP_SET;
        m->arg1 = (uint16_t)(st->costume + 1);
        m->arg2 = 1;
    }
    // A hero's idle pose and his PLAY clips run once and then rest; his
    // talking clip keeps going.
    if (m->opcode == PETKA_OP_SET || m->opcode == PETKA_OP_PLAY)
        st->rest_after_loop = m->arg1 == st->costume || m->opcode == PETKA_OP_PLAY;
    if (m->opcode == PETKA_OP_WALK) return 0; // heroes ignore WALK
    if (m->opcode == PETKA_OP_WALKED && st->walk_tail.present)
        run_tail(vm, &st->walk_tail, st->walk_sender);
    return process_base(vm, obj, q);
}

static void deliver(PetkaScriptVM *vm, PetkaObject *obj, PetkaQueued q) {
    int reacted;
    if (is_hero(obj->id)) reacted = process_hero(vm, obj, &q);
    else if (obj->is_room) reacted = process_room(vm, obj, &q);
    else reacted = process_base(vm, obj, &q);
    if (vm->host.message) vm->host.message(vm->host.user, vm, obj, &q, reacted);
}

void petka_scriptvm_send(PetkaScriptVM *vm, uint16_t obj_id, uint16_t opcode,
                         uint16_t arg1, uint16_t arg2, uint16_t arg3, int sender) {
    PetkaObject *obj = petka_script_find(vm->script, obj_id);
    if (!obj) return;
    PetkaQueued q = { { obj_id, opcode, arg1, arg2, arg3 }, sender };
    deliver(vm, obj, q);
}

// The queue is walked front to back; a message whose object is busy stays
// put and later messages overtake it (sub_405680). Messages posted while
// delivering are picked up in the same call.
int petka_scriptvm_run(PetkaScriptVM *vm) {
    int delivered = 0;
    uint32_t i = 0;
    while (i < vm->queue_count) {
        PetkaQueued q = vm->queue[i];
        PetkaObject *obj = petka_script_find(vm->script, q.msg.obj_id);
        if (obj && state_of(vm, obj)->busy) { i++; continue; }
        memmove(&vm->queue[i], &vm->queue[i + 1], (vm->queue_count - i - 1) * sizeof(PetkaQueued));
        vm->queue_count--;
        // The exe keeps a message for an unknown object queued forever;
        // dropping it is equivalent, objects are never created at runtime.
        if (!obj) continue;
        if (delivered >= RUN_LIMIT) return -1;
        deliver(vm, obj, q);
        delivered++;
    }
    return delivered;
}

int petka_scriptvm_init(PetkaScriptVM *vm) {
    // Who sends TOTALINIT was not seen in the exe. Every object has such a
    // reaction and none filters on the sender, so a broadcast from nobody
    // is the reading that fits the data.
    petka_scriptvm_broadcast(vm, PETKA_OP_TOTALINIT, 0, 0, 0, -1);
    return petka_scriptvm_run(vm);
}

void petka_scriptvm_animation_ended(PetkaScriptVM *vm, uint16_t obj_id, uint16_t resource) {
    petka_scriptvm_post(vm, obj_id, PETKA_OP_END, resource, 0, 0, -1);
}

void petka_scriptvm_forget_walk(PetkaScriptVM *vm, uint16_t hero_id) {
    PetkaObjectState *hero = petka_scriptvm_state(vm, hero_id);
    if (!hero) return;
    // The exe readdresses the queued WALKED to nobody; run() drops those.
    for (uint32_t i = 0; i < vm->queue_count; i++)
        if (vm->queue[i].msg.opcode == PETKA_OP_WALKED && vm->queue[i].msg.obj_id == hero_id)
            vm->queue[i].msg.obj_id = PETKA_NONE;
    tail_clear(&hero->walk_tail);
}

void petka_scriptvm_stop_hero(PetkaScriptVM *vm, uint16_t hero_id) {
    PetkaObjectState *hero = petka_scriptvm_state(vm, hero_id);
    if (!hero) return;
    hero->busy = 0;
    if (vm->host.stop) vm->host.stop(vm->host.user, hero_id);
    petka_scriptvm_forget_walk(vm, hero_id);
    if (!hero->rest_after_loop && hero->costume != PETKA_NONE)
        petka_scriptvm_send(vm, hero_id, PETKA_OP_SET, hero->costume, 1, 0, -1);
}

// The end of a hero's walk step routine (sub_408EF0).
void petka_scriptvm_walk_ended(PetkaScriptVM *vm, uint16_t hero_id) {
    PetkaObjectState *hero = petka_scriptvm_state(vm, hero_id);
    if (!hero) return;
    PetkaTail *tail = &hero->walk_tail;
    int leaving = 0;
    for (uint32_t i = 0; tail->present && i < tail->count && !leaving; i++)
        leaving = tail->messages[i].opcode == PETKA_OP_GOTO || tail->messages[i].opcode == PETKA_OP_SETSEQ;

    if (hero->costume == PETKA_NONE) {
        // No costume yet: nothing to stand in.
    } else if (!leaving) {
        petka_scriptvm_send(vm, hero_id, PETKA_OP_SET, hero->costume, 1, 0, -1);
    } else {
        // About to walk out of the room: hold the walking pose rather than
        // flash the idle one, and stand up once the reaction has moved on.
        petka_scriptvm_send(vm, hero_id, PETKA_OP_ANIMATE, 0, 0, 0, -1);
        PetkaMessage after[2] = {
            { hero_id, PETKA_OP_SET, hero->costume, 1, 0 },
            { hero_id, PETKA_OP_ANIMATE, 1, 0, 0 },
        };
        tail->messages = (PetkaMessage *)realloc(tail->messages, (tail->count + 2) * sizeof(PetkaMessage));
        memcpy(tail->messages + tail->count, after, sizeof(after));
        tail->count += 2;
    }
    hero->busy = 0;
}

// sub_4165A0: the talker gets SAID at once, not through the queue, so it is
// back in its idle pose before the parked reaction goes on.
void petka_scriptvm_dialogue_ended(PetkaScriptVM *vm, uint16_t talker) {
    if (talker != PETKA_NONE) petka_scriptvm_send(vm, talker, PETKA_OP_SAID, 0, 0, 0, -1);
    vm->interface_enabled = 1;
    run_tail(vm, &vm->dialog_tail, vm->dialog_sender);
    vm->dialog_sender = -1;
}
