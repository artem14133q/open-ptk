// Reader + message VM for Petka's game script.
// Format and semantics reverse-engineered from Petka.exe (Base.cpp) -- see
// docs/str-format.md for the derivation.
//
// PART_N/SCRIPT.DAT   -- every object and room of a part, each with a list
//                        of reactions; a reaction is a trigger (opcode,
//                        status, sender) plus the messages it posts.
// PART_N/BACKGRND.BG  -- for each room, the objects that live in it.
//
// Pure C, no platform dependencies: builds for desktop and Switch alike.
#ifndef PETKA_SCRIPT_H
#define PETKA_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opcodes 0..46 carry the names found in Petka.exe (sub_402C70). The exe
// has no names for 47..62; those are ours, derived from the handlers.
typedef enum {
    PETKA_OP_USE = 1,
    PETKA_OP_SETPOS = 2,
    PETKA_OP_GOTO = 3,
    PETKA_OP_LOOK = 4,
    PETKA_OP_SAY = 5,
    PETKA_OP_TAKE = 6,
    PETKA_OP_WALK = 9,
    PETKA_OP_TALK = 10,
    PETKA_OP_END = 11,
    PETKA_OP_SET = 14,
    PETKA_OP_SHOW = 15,
    PETKA_OP_HIDE = 16,
    PETKA_OP_DIALOG = 17,
    PETKA_OP_ZBUFFER = 18,
    PETKA_OP_TOTALINIT = 19,
    PETKA_OP_ANIMATE = 20,
    PETKA_OP_STATUS = 21,
    PETKA_OP_ADDINV = 22,
    PETKA_OP_DELINV = 23,
    PETKA_OP_STOP = 24, // the exe labels both 24 and 40 "SYSTEM"
    PETKA_OP_CURSOR = 25,
    PETKA_OP_OBJECTUSE = 26,
    PETKA_OP_ACTIVE = 27,
    PETKA_OP_SAID = 28,
    PETKA_OP_SETSEQ = 29,
    PETKA_OP_ENDSEQ = 30,
    PETKA_OP_CHECK = 31,
    PETKA_OP_IF = 32,
    PETKA_OP_SHOWDESCRIPTION = 33,
    PETKA_OP_HALF = 34,
    PETKA_OP_WALKED = 35,
    PETKA_OP_WALKTO = 36,
    PETKA_OP_WALKVICH = 37,
    PETKA_OP_INITBG = 38,
    PETKA_OP_USERMSG = 39,
    PETKA_OP_SYSTEM = 40,
    PETKA_OP_CONTINUE = 42,
    PETKA_OP_MAP = 43,
    PETKA_OP_PASSIVE = 44,
    PETKA_OP_NOMAP = 45,
    PETKA_OP_SETINV = 46,
    PETKA_OP_BGSFX = 47,
    PETKA_OP_MUSIC = 48,
    PETKA_OP_IMAGE = 49,
    PETKA_OP_STAND = 50,
    PETKA_OP_ON = 51,
    PETKA_OP_OFF = 52,
    PETKA_OP_PLAY = 53,
    PETKA_OP_LEAVEBG = 54,
    PETKA_OP_55 = 55, // sets a flag on the screen object; meaning unknown
    PETKA_OP_RANDOM = 57,
    PETKA_OP_JUMP = 58,
    PETKA_OP_JUMPVICH = 59,
    PETKA_OP_PART = 60,
    PETKA_OP_CHAPTER = 61,
    PETKA_OP_AVI = 62,
} PetkaOpcode;

// Never NULL; unknown opcodes come back as "OP_<n>" in a static buffer.
const char *petka_opcode_name(uint16_t opcode);

#define PETKA_NONE 0xFFFFu // "-1" in every u16 field of the script

// Both heroes are ordinary script objects with fixed ids in every part.
#define PETKA_ID_PETKA 0u
#define PETKA_ID_CHAPAYEV 1u

// ---- SCRIPT.DAT + BACKGRND.BG ----

typedef struct {
    uint16_t obj_id; // target object
    uint16_t opcode;
    uint16_t arg1, arg2, arg3;
} PetkaMessage;

typedef struct {
    uint16_t opcode;
    uint8_t status;   // 0xFF = any object status
    uint16_t sender;  // PETKA_NONE = any sender
    PetkaMessage *messages; // owned
    uint32_t message_count;
} PetkaReaction;

typedef struct {
    uint16_t obj_id;
    int32_t x, y;
    int32_t z;              // draw order, -1 = unset
    int32_t walk_x, walk_y; // where a hero stands to interact, -1 = unset
} PetkaAttach;

typedef struct {
    uint16_t id;
    char *name; // owned, CP1251
    int is_room;
    PetkaReaction *reactions; // owned
    uint32_t reaction_count;

    // Rooms only: the objects living in this room (from BACKGRND.BG).
    PetkaAttach *attached; // owned
    uint32_t attached_count;
} PetkaObject;

typedef struct {
    PetkaObject *objects; // plain objects first, then rooms (file order)
    uint32_t object_count;
    uint32_t room_count;
} PetkaScript;

// Parses SCRIPT.DAT and, if bg_data is non-NULL, BACKGRND.BG. Returns NULL
// on failure, including "doesn't consume the file exactly" -- neither format
// has another integrity check, so exact EOF alignment is the validity check.
PetkaScript *petka_script_open(const uint8_t *script_data, size_t script_len,
                               const uint8_t *bg_data, size_t bg_len);
void petka_script_close(PetkaScript *script);

// Returns NULL if not found.
PetkaObject *petka_script_find(const PetkaScript *script, uint16_t id);
// Case-sensitive CP1251 byte compare (BGS.INI refers to objects by name).
PetkaObject *petka_script_find_name(const PetkaScript *script, const char *name);

// ---- message VM ----

typedef struct {
    PetkaMessage *messages; // owned
    uint32_t count;
    int present; // an empty tail is still "present" (the exe allocates it)
} PetkaTail;

// Runtime state of one object. Field comments give the offset in the exe's
// object where the meaning is not obvious.
typedef struct {
    uint8_t status;      // +76
    uint16_t resource;   // +4, RESOURCE.QRC id currently shown
    int visible;         // +44
    int active;          // +60, reacts to the cursor
    int animate;         // +48
    int repeat_sound;    // +72, the clip's sound restarts on every loop
    int busy;            // +56, queued messages wait while set
    int32_t zbuffer;     // +52
    int32_t z;           // +8
    int32_t x, y;
    int32_t walk_x, walk_y; // +20, +24
    uint16_t room;       // owning room, PETKA_NONE for unattached objects

    PetkaTail play_tail; // +116, rest of a reaction waiting for END
    uint16_t play_wait;  // +120, resource id that END must name

    // Rooms only.
    int on_map;          // +144
    uint16_t bgsfx;      // +148
    uint16_t music;      // +152

    // Heroes only.
    uint16_t costume;    // +148, base resource id: +1 talk, +10..17 walk
    int rest_after_loop; // +124, the clip pauses on its first frame after
                         // one loop: set for the idle pose and for PLAY
    PetkaTail walk_tail; // +172, rest of a reaction waiting for WALKED
    int walk_sender;     // +176
} PetkaObjectState;

// A message in flight. sender is an object id, or -1 for "nobody".
typedef struct {
    PetkaMessage msg;
    int sender;
} PetkaQueued;

typedef struct PetkaScriptVM PetkaScriptVM;

// Everything the VM cannot do itself. Any callback may be NULL.
typedef struct {
    void *user;

    // Is there a DIALOGUE.FIX entry for this object and verb? verb_key is
    // the opcode, or (item_id << 16) | 26 for OBJECTUSE. Without this
    // callback the VM assumes there is none.
    int (*has_dialogue)(void *user, uint16_t obj_id, uint32_t verb_key);
    // Make that entry the current one (the next DIALOG message plays it).
    void (*select_dialogue)(void *user, uint16_t obj_id, uint32_t verb_key);
    // Start the current dialogue. Call petka_scriptvm_dialogue_ended() once
    // it is over.
    void (*start_dialogue)(void *user, uint16_t obj_id, uint16_t arg);

    // Start walking a hero. Return non-zero if the walk actually started;
    // the hero is then busy until petka_scriptvm_walk_ended(). Returning 0
    // (already there, hero hidden) lets the script continue at once.
    int (*walk)(void *user, uint16_t hero_id, int32_t x, int32_t y);

    // The current room changed; prev_room is PETKA_NONE for the first one.
    // The host places the heroes at the entry point named by BGS.INI.
    void (*room_changed)(void *user, uint16_t prev_room, uint16_t room);

    // Every delivered message, after the VM's own handling. This is where
    // the host starts animations and sounds, plays AVIs, switches part, ...
    void (*message)(void *user, PetkaScriptVM *vm, const PetkaObject *obj,
                    const PetkaQueued *q, int reacted);
} PetkaScriptHost;

struct PetkaScriptVM {
    PetkaScript *script; // not owned; RANDOM rewrites its messages in place
    PetkaObjectState *state; // owned, parallel to script->objects
    PetkaScriptHost host;

    PetkaQueued *queue; // owned
    uint32_t queue_count, queue_cap;

    PetkaTail dialog_tail; // rest of a reaction waiting for the dialogue
    int dialog_sender;

    uint16_t room; // current room id, PETKA_NONE before the first GOTO

    uint16_t *inventory; // owned, object ids in pickup order
    uint32_t inventory_count, inventory_cap;

    int interface_enabled;
    uint16_t cursor_resource; // PETKA_NONE = default cursor
    uint16_t cursor_object;   // inventory object carried by the cursor

    uint32_t rng; // RANDOM state; seed it for reproducible runs
};

// Returns NULL only on allocation failure. `host` may be NULL.
PetkaScriptVM *petka_scriptvm_open(PetkaScript *script, const PetkaScriptHost *host);
void petka_scriptvm_close(PetkaScriptVM *vm);

PetkaObjectState *petka_scriptvm_state(PetkaScriptVM *vm, uint16_t id);

// Queue a message for one object / for every object.
void petka_scriptvm_post(PetkaScriptVM *vm, uint16_t obj_id, uint16_t opcode,
                         uint16_t arg1, uint16_t arg2, uint16_t arg3, int sender);
void petka_scriptvm_broadcast(PetkaScriptVM *vm, uint16_t opcode, uint16_t arg1,
                              uint16_t arg2, uint16_t arg3, int sender);
// Deliver a message right now, bypassing the queue.
void petka_scriptvm_send(PetkaScriptVM *vm, uint16_t obj_id, uint16_t opcode,
                         uint16_t arg1, uint16_t arg2, uint16_t arg3, int sender);

// Deliver queued messages until the queue is empty or everything left is
// waiting on a busy object. Returns the number delivered, or -1 if the
// script looped without ever waiting (the queue is left as it was).
int petka_scriptvm_run(PetkaScriptVM *vm);

// Broadcast TOTALINIT and run it. Call once after opening a part.
int petka_scriptvm_init(PetkaScriptVM *vm);

// Make `room` current: broadcasts LEAVEBG from the old room and INITBG from
// the new one (unless `silent`, used when restoring a save). The messages
// are only queued; call petka_scriptvm_run() afterwards.
void petka_scriptvm_enter_room(PetkaScriptVM *vm, uint16_t room, int silent);

// Host notifications.
// A clip reached its last frame: posts END (resource, 0, 0) from nobody, as
// the exe's object update does on every loop of the clip.
void petka_scriptvm_animation_ended(PetkaScriptVM *vm, uint16_t obj_id, uint16_t resource);
void petka_scriptvm_walk_ended(PetkaScriptVM *vm, uint16_t hero_id);
// talker is the object that spoke last, or PETKA_NONE.
void petka_scriptvm_dialogue_ended(PetkaScriptVM *vm, uint16_t talker);

int petka_scriptvm_has_item(const PetkaScriptVM *vm, uint16_t obj_id);

#ifdef __cplusplus
}
#endif

#endif
