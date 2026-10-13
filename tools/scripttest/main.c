// Host-only dev tool: parse a PART_N's SCRIPT.DAT + BACKGRND.BG from a
// store (usually MAIN.STR) and run the message VM headless, to validate the
// parser and the VM against real data. Animations and dialogues finish
// instantly, so a whole cutscene plays out in one go.
#include "petka/script.h"
#include "petka/store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CP1251 -> UTF-8 for terminal output (Cyrillic block only).
static const char *utf8(const char *s) {
    static char bufs[4][512];
    static int next;
    char *out = bufs[next++ & 3], *p = out;
    for (; *s && p < out + 508; s++) {
        unsigned char c = (unsigned char)*s;
        unsigned cp = c < 0x80 ? c : c >= 0xC0 ? 0x410u + (c - 0xC0) : c == 0xA8 ? 0x401u : c == 0xB8 ? 0x451u : '?';
        if (cp < 0x80) *p++ = (char)cp;
        else { *p++ = (char)(0xC0 | cp >> 6); *p++ = (char)(0x80 | (cp & 0x3F)); }
    }
    *p = 0;
    return out;
}

static const char *obj_name(const PetkaScript *script, int id) {
    PetkaObject *obj = id >= 0 ? petka_script_find(script, (uint16_t)id) : NULL;
    return obj ? utf8(obj->name) : "-";
}

typedef struct {
    int trace;
    int dialogue_pending;
} Host;

static void on_message(void *user, PetkaScriptVM *vm, const PetkaObject *obj,
                       const PetkaQueued *q, int reacted) {
    Host *host = (Host *)user;
    const PetkaMessage *m = &q->msg;
    // A broadcast reaches every object; only show the ones that react.
    int noise = !reacted && (m->opcode == PETKA_OP_INITBG || m->opcode == PETKA_OP_LEAVEBG);
    if (host->trace && !noise) {
        printf("  %-16s -> %u[%s]  %d %d %d  from %d[%s]%s\n", petka_opcode_name(m->opcode),
               obj->id, utf8(obj->name), (int16_t)m->arg1, (int16_t)m->arg2, (int16_t)m->arg3,
               q->sender, obj_name(vm->script, q->sender), reacted ? "  (reaction)" : "");
    }
    // Pretend the animation is over as soon as it starts.
    if (m->opcode == PETKA_OP_PLAY) petka_scriptvm_animation_ended(vm, obj->id, m->arg1);
}

static void on_start_dialogue(void *user, uint16_t obj_id, uint16_t arg) {
    (void)obj_id; (void)arg;
    ((Host *)user)->dialogue_pending = 1;
}

// Runs the queue dry, ending every dialogue the script starts on the way.
static int run_all(PetkaScriptVM *vm, Host *host) {
    int total = 0;
    for (;;) {
        int n = petka_scriptvm_run(vm);
        if (n < 0) { printf("  !! script looped without waiting\n"); return -1; }
        total += n;
        if (!host->dialogue_pending) break;
        host->dialogue_pending = 0;
        if (host->trace) printf("  (dialogue ends)\n");
        petka_scriptvm_dialogue_ended(vm, PETKA_NONE);
    }
    return total;
}

static const uint8_t *entry(PetkaStore *store, const char *part, const char *file, size_t *len) {
    char name[256];
    snprintf(name, sizeof(name), "%s\\%s", part, file);
    const PetkaEntry *e = petka_store_find(store, name);
    if (!e) { fprintf(stderr, "entry not found: %s\n", name); return NULL; }
    return petka_store_entry_data(store, e, len);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <store.str> <part_prefix e.g. PART_1> [room_id [obj_id opcode]]\n"
                "  no room_id:     print script statistics and run TOTALINIT\n"
                "  room_id:        also enter that room and trace its INITBG\n"
                "  obj_id opcode:  then send that message (e.g. 1 = USE) and trace it\n",
                argv[0]);
        return 1;
    }
    PetkaStore *store = petka_store_open(argv[1]);
    if (!store) { fprintf(stderr, "open failed\n"); return 1; }

    size_t script_len = 0, bg_len = 0;
    const uint8_t *script_data = entry(store, argv[2], "SCRIPT.DAT", &script_len);
    const uint8_t *bg_data = entry(store, argv[2], "BACKGRND.BG", &bg_len);
    if (!script_data || !bg_data) return 1;

    PetkaScript *script = petka_script_open(script_data, script_len, bg_data, bg_len);
    if (!script) { fprintf(stderr, "parse failed (not byte-exact)\n"); return 1; }

    uint32_t reactions = 0, messages = 0, attached = 0;
    for (uint32_t i = 0; i < script->object_count; i++) {
        const PetkaObject *obj = &script->objects[i];
        reactions += obj->reaction_count;
        attached += obj->attached_count;
        for (uint32_t k = 0; k < obj->reaction_count; k++) messages += obj->reactions[k].message_count;
    }
    printf("objects: %u (rooms: %u)  reactions: %u  messages: %u  attachments: %u\n",
           script->object_count, script->room_count, reactions, messages, attached);

    Host host = { 0, 0 };
    PetkaScriptHost callbacks = { 0 };
    callbacks.user = &host;
    callbacks.message = on_message;
    callbacks.start_dialogue = on_start_dialogue;
    PetkaScriptVM *vm = petka_scriptvm_open(script, &callbacks);

    petka_scriptvm_broadcast(vm, PETKA_OP_TOTALINIT, 0, 0, 0, -1);
    int n = run_all(vm, &host);
    uint32_t hidden = 0, with_resource = 0;
    for (uint32_t i = 0; i < script->object_count; i++) {
        hidden += !vm->state[i].visible;
        with_resource += vm->state[i].resource != PETKA_NONE;
    }
    printf("TOTALINIT: %d messages delivered, %u objects have a resource, %u hidden, queue left: %u\n",
           n, with_resource, hidden, vm->queue_count);

    if (argc >= 4) {
        uint16_t room_id = (uint16_t)atoi(argv[3]);
        PetkaObject *room = petka_script_find(script, room_id);
        if (!room || !room->is_room) { fprintf(stderr, "no such room: %u\n", room_id); return 1; }
        host.trace = 1;
        printf("\nenter room %u \"%s\" (%u objects)\n", room_id, utf8(room->name), room->attached_count);
        petka_scriptvm_enter_room(vm, room_id, 0);
        run_all(vm, &host);
        printf("now in room %u \"%s\"\n", vm->room, obj_name(script, vm->room));

        if (argc >= 6) {
            uint16_t obj_id = (uint16_t)atoi(argv[4]);
            uint16_t opcode = (uint16_t)atoi(argv[5]);
            PetkaObjectState *st = petka_scriptvm_state(vm, obj_id);
            if (!st) { fprintf(stderr, "no such object: %u\n", obj_id); return 1; }
            printf("\n%s on %u \"%s\" (status %u)\n", petka_opcode_name(opcode), obj_id,
                   obj_name(script, obj_id), st->status);
            // As a click does it: no arguments, the object is its own sender.
            petka_scriptvm_post(vm, obj_id, opcode, 0, 0, 0, obj_id);
            run_all(vm, &host);
            printf("status now %u, visible %d, resource %d, cursor %s, room %u \"%s\"\n",
                   st->status, st->visible, (int16_t)st->resource,
                   vm->cursor_visible ? "on" : "off", vm->room, obj_name(script, vm->room));
            printf("inventory:");
            for (uint32_t i = 0; i < vm->inventory_count; i++)
                printf(" %u[%s]", vm->inventory[i], obj_name(script, vm->inventory[i]));
            printf("\n");
        }
    }

    petka_scriptvm_close(vm);
    petka_script_close(script);
    petka_store_close(store);
    return 0;
}
