// scriptfix.c
//
// EN: Fixes to the game's own script: its mistakes put right as it is loaded.
// RU: Исправления сценария игры: его ошибки исправляются при загрузке.
#include "game.h"

// A fix: in that part, in the reaction of `obj` to `op` (in `status`, from
// `sender`; PETKA_NONE for any), after its first message `after_op` to
// `after_to`, a message `add_op` with `add_arg1` to `add_to` is added. A
// saved game whose `add_to` has the resource `stale` (and is hidden) gets
// `add_arg1` instead.
static const struct {
    uint8_t part;
    uint16_t obj, op, status, sender;
    uint16_t after_to, after_op;
    uint16_t add_to, add_op, add_arg1;
    uint16_t stale;
} kFixes[] = {
    // The hive with the bees at the apiary, the syringe used on it: PLAY
    // puts the clip of Petka filling the syringe on it, HIDE hides it, and
    // its own clip (ULEI1.FLC, from its TOTALINIT) is never set again. Hidden
    // but active, it was clicked -- and named, and outlined -- by Petka's
    // shape where he had stood. Its clip back after the HIDE.
    {1, 304, PETKA_OP_OBJECTUSE, 1, 39, 304, PETKA_OP_HIDE, 304, PETKA_OP_SET, 1108, 1128},
    // The keyhole of the bath, the syringe with the bees used on it: the
    // keyhole's own dialogue for it ("Ай да Петька!... Молдец, Петька.
    // Хвалю!"), which the reaction never says, as Reloaded has it -- once
    // the anarchists' sequence (542) is over: the clip that ends it (436's
    // END) ends it, and then the talk. The whole dialogue, as the second
    // part of restored.c's kSplit with nothing before it; with the extras
    // switch on (conversation.c).
    {1, 436, PETKA_OP_END, 255, PETKA_NONE, 542, PETKA_OP_ENDSEQ, 128, PETKA_OP_DIALOG, RESTORED_REST, 0},
    // The church's hole, the brick put in it, and the brick pulled: the
    // crypt's door opens in the sequence of the graveyard (748), which the
    // door's clip ends (573's END). Then the hole's own dialogue for the
    // brick ("Ой, Василь Иваныч, слышь, скрежет-то какой..."), which no
    // reaction says, as Reloaded has it once the heroes are back in the
    // church -- all of it, as the second part of restored.c's kSplit; with
    // the extras switch on (conversation.c).
    {2, 573, PETKA_OP_END, 255, PETKA_NONE, 748, PETKA_OP_ENDSEQ, 549, PETKA_OP_DIALOG, RESTORED_REST, 0},
    // The fireplace's lamp in the White HQ, turned by Chapayev: his "Оторвать
    // не получилось, но свернул я ее..." (the lamp's own dialogue, which the
    // reaction never says) tells of what he has done, so it comes after the
    // clip and the left lamp's lighting, not before them (restored.c) --
    // with the extras switch on (conversation.c).
    {3, 63, PETKA_OP_OBJECTUSE, 0, 1, 64, PETKA_OP_STATUS, 63, PETKA_OP_DIALOG, RESTORED_LATE, 0},
    // ...and before he goes to it, Petka's question (restored.c's kPhrases).
    {3, 63, PETKA_OP_OBJECTUSE, 0, 1, 63, PETKA_OP_STOP, 63, PETKA_OP_DIALOG, RESTORED_PHRASES, 0},
    // The rope at the jetty, cut with the sword (its reactions send USERMSG
    // to "Петька на берегу реки", 652, who does it): the rope's own
    // dialogue, which none of them says, in two parts -- Petka's line once
    // he has walked to the rope, before the cut's clip; Chapayev's once the
    // clip has ended and Petka is back (restored.c's kSplit).
    {2, 652, PETKA_OP_USERMSG, 0, PETKA_NONE, 652, PETKA_OP_WALKTO, 653, PETKA_OP_DIALOG, RESTORED_FIRST, 0},
    {2, 652, PETKA_OP_END, PETKA_NONE, PETKA_NONE, 0, PETKA_OP_ON, 653, PETKA_OP_DIALOG, RESTORED_REST, 0},
};

#define M(to, op, a1, a2) {.obj_id = (to), .opcode = PETKA_OP_##op, .arg1 = (a1), .arg2 = (a2), .arg3 = PETKA_NONE}
#define NONE PETKA_NONE

// The second game, part 1, DUNGEON 0 (475) first come into, down the well:
// the exe goes straight to the talk (SpDialog15). Its clips of the heroes
// landing are in RESOURCE.QRC and used by nothing -- Petka getting up from
// the floor (WSTAET.FLC, 4031) and Chapayev jumping down after him and
// looking round through his binoculars (PRIGNUL.FLC, 4032) -- and Reloaded
// plays them together before the talk. Here too, with the extras switch on:
// the heroes hidden, the two clips on the room's own clip objects (312 and
// 314, the ball's scene later in the room), Chapayev held on his last frame
// (kReactions2) till Petka's has ended; then the heroes stand where the
// clips leave them, and the objects get their clips back.
static const PetkaMessage kDungeonLanding[] = {
    M(475, SYSTEM, 0, NONE),
    M(PETKA_ID_PETKA, OFF, NONE, NONE),
    M(PETKA_ID_CHAPAYEV, OFF, NONE, NONE),
    M(314, SET, 4032, 1),
    M(314, ON, NONE, NONE),
    M(312, ON, NONE, NONE),
    M(312, PLAY, 4031, NONE),
    M(312, OFF, NONE, NONE),
    M(314, OFF, NONE, NONE),
    M(312, SET, 4010, NONE),
    M(314, SET, 4015, NONE),
    M(314, ANIMATE, 1, NONE),
    M(312, JUMP, 152, 321),
    M(314, JUMPVICH, 95, 303),
    M(PETKA_ID_PETKA, ON, NONE, NONE),
    M(PETKA_ID_CHAPAYEV, ON, NONE, NONE),
    M(475, SYSTEM, 1, NONE),
};

// DUNGEON 1 (476), the room of the red sign and the relief, first come
// into: the exe has no reaction to it. Its clip of the heroes coming out of
// the passage, Chapayev looking the room over through his binoculars
// (P&W.FLC, 3017), is used by nothing; Reloaded plays it then. Here too, with
// the extras switch on, on the hidden relief (336), which gets its own clip
// back after; the room's status, which kReactions2's reaction sets before
// it, keeps it to the first time.
static const PetkaMessage kDungeonLookRound[] = {
    M(476, SYSTEM, 0, NONE),
    M(PETKA_ID_PETKA, OFF, NONE, NONE),
    M(PETKA_ID_CHAPAYEV, OFF, NONE, NONE),
    M(336, SHOW, NONE, NONE),
    M(336, PLAY, 3017, NONE),
    M(336, SET, 3015, NONE),
    M(336, HIDE, NONE, NONE),
    M(336, JUMP, 268, 449),
    M(336, JUMPVICH, 355, 395),
    M(PETKA_ID_PETKA, ON, NONE, NONE),
    M(PETKA_ID_CHAPAYEV, ON, NONE, NONE),
};

// DUNGEON ROOM 1 (483), the five pillars: the exe has a thing put on one
// without Petka moving. Reloaded has him reach out to it -- his own clips,
// which the exe has him play to take the fly off the pillar: reaching
// (pg_b, 19000), the thing there, back (pg_b___r, 19001) and turning to us
// (pt_b_f, 20901); for the tall pillar of water reaching up (pg_f_up and
// pg_f_upr, 19010 and 19011), facing us all along.
// He goes up to it first, as Reloaded has him: the pillars have no walk
// point (their WALKTO takes him nowhere). The altar's is the one the exe
// has him go to for taking the fly off it; the others' are found the same
// way, a little in front of the pillar's foot -- to the right of the tall
// pillar of water, which goes down to the bottom of the screen, his
// raised hand at its top.
static const PetkaMessage kPillarWalk1[] = {M(348, WALKTO, 360, 400)};
static const PetkaMessage kPillarWalk2[] = {M(351, WALKTO, 115, 425)};
static const PetkaMessage kPillarWalk3[] = {M(354, WALKTO, 483, 361)};
static const PetkaMessage kPillarWalk4[] = {M(357, WALKTO, 163, 350)};
static const PetkaMessage kPillarWalk5[] = {M(360, WALKTO, 600, 380)};
static const PetkaMessage kPillarReach[] = {
    M(PETKA_ID_PETKA, PLAY, 19000, NONE),
};
static const PetkaMessage kPillarBack[] = {
    M(PETKA_ID_PETKA, PLAY, 19001, NONE),
    M(PETKA_ID_PETKA, PLAY, 20901, NONE),
    M(PETKA_ID_PETKA, STAND, NONE, NONE),
};
static const PetkaMessage kPillarReachUp[] = {
    M(PETKA_ID_PETKA, PLAY, 19010, NONE),
};
static const PetkaMessage kPillarBackUp[] = {
    M(PETKA_ID_PETKA, PLAY, 19011, NONE),
    M(PETKA_ID_PETKA, STAND, NONE, NONE),
};

// ...and its far passage (EXIT 2, 344), to DUNGEON ROOM 2: the exe goes
// there at once. Its clips of the heroes climbing in are used by nothing --
// Petka's (P-LESET.FLC, 4132), then Chapayev's once he has come up to it
// (W-LESET.FLC, 4133) -- and Reloaded plays them; here on the passage
// itself, which gets its own clip back before the room is left.
static const PetkaMessage kPassageClimb[] = {
    M(344, SYSTEM, 0, NONE),
    M(PETKA_ID_PETKA, OFF, NONE, NONE),
    M(344, SHOW, NONE, NONE),
    M(344, PLAY, 4132, NONE),
    M(344, HIDE, NONE, NONE),
    M(344, WALKVICH, 334, 284),
    M(PETKA_ID_CHAPAYEV, OFF, NONE, NONE),
    M(344, SHOW, NONE, NONE),
    M(344, PLAY, 4133, NONE),
    M(344, SET, 4130, NONE),
    M(344, HIDE, NONE, NONE),
    M(PETKA_ID_PETKA, ON, NONE, NONE),
    M(PETKA_ID_CHAPAYEV, ON, NONE, NONE),
    M(344, SYSTEM, 1, NONE),
};

#define N(a) a, sizeof(a) / sizeof((a)[0])

// Messages put in before a reaction's first message `before_op` to
// `before_to`, behind a PETKA_OP_RESTORED: taken with the extras switch on.
static const struct {
    uint8_t part;
    uint16_t obj, op, status, sender;
    uint16_t before_to, before_op;
    const PetkaMessage *messages;
    uint32_t count;
} kInserts2[] = {
    {1, 475, PETKA_OP_INITBG, 0, 475, 18, PETKA_OP_TALK, kDungeonLanding,
     sizeof(kDungeonLanding) / sizeof(kDungeonLanding[0])},
    {1, 476, PETKA_OP_INITBG, 0, 476, 476, PETKA_OP_SYSTEM, kDungeonLookRound,
     sizeof(kDungeonLookRound) / sizeof(kDungeonLookRound[0])},
    // The pillars, each with the thing it takes: love (the rubber woman),
    // fire (the vat of lava), earth (the pot), air (the plane), water (the
    // spirit).
    {1, 348, PETKA_OP_OBJECTUSE, 1, 38, 348, PETKA_OP_CURSOR, N(kPillarWalk1)},
    {1, 351, PETKA_OP_OBJECTUSE, 0, 39, 351, PETKA_OP_CURSOR, N(kPillarWalk2)},
    {1, 354, PETKA_OP_OBJECTUSE, 0, 44, 354, PETKA_OP_CURSOR, N(kPillarWalk3)},
    {1, 357, PETKA_OP_OBJECTUSE, 0, 42, 357, PETKA_OP_CURSOR, N(kPillarWalk4)},
    {1, 360, PETKA_OP_OBJECTUSE, 0, 40, 360, PETKA_OP_CURSOR, N(kPillarWalk5)},
    {1, 348, PETKA_OP_OBJECTUSE, 1, 38, 350, PETKA_OP_SET, N(kPillarReach)},
    {1, 348, PETKA_OP_OBJECTUSE, 1, 38, 348, PETKA_OP_STATUS, N(kPillarBack)},
    {1, 351, PETKA_OP_OBJECTUSE, 0, 39, 353, PETKA_OP_SHOW, N(kPillarReach)},
    {1, 351, PETKA_OP_OBJECTUSE, 0, 39, 351, PETKA_OP_STATUS, N(kPillarBack)},
    {1, 354, PETKA_OP_OBJECTUSE, 0, 44, 356, PETKA_OP_SHOW, N(kPillarReach)},
    {1, 354, PETKA_OP_OBJECTUSE, 0, 44, 354, PETKA_OP_STATUS, N(kPillarBack)},
    {1, 357, PETKA_OP_OBJECTUSE, 0, 42, 359, PETKA_OP_SHOW, N(kPillarReach)},
    {1, 357, PETKA_OP_OBJECTUSE, 0, 42, 357, PETKA_OP_STATUS, N(kPillarBack)},
    {1, 360, PETKA_OP_OBJECTUSE, 0, 40, 362, PETKA_OP_SHOW, N(kPillarReachUp)},
    {1, 360, PETKA_OP_OBJECTUSE, 0, 40, 360, PETKA_OP_STATUS, N(kPillarBackUp)},
    {1, 344, PETKA_OP_WALK, 0xFF, NONE, 484, PETKA_OP_GOTO, N(kPassageClimb)},
};

#undef N

// Chapayev's landing ends on its last frame, held there.
static const PetkaMessage kLandingHeld[] = {
    M(314, IF, 4032, NONE),
    M(314, ANIMATE, 0, NONE),
};

// DUNGEON 1 first come into: its status set first, as the exe's rooms do
// (DUNGEON 0), so that kDungeonLookRound -- put in before the interface
// comes back -- is played once whatever becomes of it. From the room
// itself, as the exe's rooms take their INITBG: it goes to every object
// when any room is come into.
static const PetkaMessage kDungeon1Entered[] = {
    M(476, STATUS, 1, NONE),
    M(476, SYSTEM, 1, NONE),
};

// Reactions added to an object (before kInserts2 is put in).
static const struct {
    uint8_t part;
    uint16_t obj, op;
    uint8_t status;
    uint16_t sender;
    const PetkaMessage *messages;
    uint32_t count;
} kReactions2[] = {
    {1, 314, PETKA_OP_END, 0xFF, NONE, kLandingHeld, sizeof(kLandingHeld) / sizeof(kLandingHeld[0])},
    {1, 476, PETKA_OP_INITBG, 0, 476, kDungeon1Entered, sizeof(kDungeon1Entered) / sizeof(kDungeon1Entered[0])},
};

#undef M
#undef NONE

static void scriptfix_script2(Game *g) {
    for (size_t f = 0; f < sizeof(kReactions2) / sizeof(kReactions2[0]); f++) {
        if (kReactions2[f].part != g->part_number) continue;
        PetkaObject *obj = petka_script_find(g->script, kReactions2[f].obj);
        if (!obj) continue;
        uint32_t n = kReactions2[f].count;
        PetkaReaction *res =
            (PetkaReaction *)realloc(obj->reactions, (obj->reaction_count + 1) * sizeof(PetkaReaction));
        PetkaMessage *msgs = (PetkaMessage *)malloc(n * sizeof(PetkaMessage));
        if (!res || !msgs) { free(msgs); return; }
        obj->reactions = res;
        memcpy(msgs, kReactions2[f].messages, n * sizeof(PetkaMessage));
        res[obj->reaction_count++] = (PetkaReaction){.opcode = kReactions2[f].op, .status = kReactions2[f].status,
                                                     .sender = kReactions2[f].sender, .messages = msgs,
                                                     .message_count = n};
    }
    for (size_t f = 0; f < sizeof(kInserts2) / sizeof(kInserts2[0]); f++) {
        if (kInserts2[f].part != g->part_number) continue;
        PetkaObject *obj = petka_script_find(g->script, kInserts2[f].obj);
        for (uint32_t r = 0; obj && r < obj->reaction_count; r++) {
            PetkaReaction *re = &obj->reactions[r];
            if (re->opcode != kInserts2[f].op || re->status != kInserts2[f].status ||
                re->sender != kInserts2[f].sender)
                continue;
            for (uint32_t m = 0; m < re->message_count; m++) {
                if (re->messages[m].obj_id != kInserts2[f].before_to ||
                    re->messages[m].opcode != kInserts2[f].before_op)
                    continue;
                uint32_t n = kInserts2[f].count;
                PetkaMessage *msgs =
                    (PetkaMessage *)realloc(re->messages, (re->message_count + n + 1) * sizeof(PetkaMessage));
                if (!msgs) return;
                memmove(msgs + m + n + 1, msgs + m, (re->message_count - m) * sizeof(PetkaMessage));
                msgs[m] = (PetkaMessage){.obj_id = kInserts2[f].obj, .opcode = PETKA_OP_RESTORED, .arg1 = (uint16_t)n,
                                         .arg2 = PETKA_NONE, .arg3 = PETKA_NONE};
                memcpy(msgs + m + 1, kInserts2[f].messages, n * sizeof(PetkaMessage));
                re->messages = msgs;
                re->message_count += n + 1;
                break;
            }
        }
    }
}

void scriptfix_script(Game *g) {
    if (g->game == 2) { scriptfix_script2(g); return; }
    if (g->game != 1) return; // kFixes are the first game's
    for (size_t f = 0; f < sizeof(kFixes) / sizeof(kFixes[0]); f++) {
        if (kFixes[f].part != g->part_number) continue;
        PetkaObject *obj = petka_script_find(g->script, kFixes[f].obj);
        for (uint32_t r = 0; obj && r < obj->reaction_count; r++) {
            PetkaReaction *re = &obj->reactions[r];
            if (re->opcode != kFixes[f].op) continue;
            if (kFixes[f].status != PETKA_NONE && re->status != kFixes[f].status) continue;
            if (kFixes[f].sender != PETKA_NONE && re->sender != kFixes[f].sender) continue;
            for (uint32_t m = 0; m < re->message_count; m++) {
                if (re->messages[m].obj_id != kFixes[f].after_to || re->messages[m].opcode != kFixes[f].after_op) continue;
                PetkaMessage *msgs =
                    (PetkaMessage *)realloc(re->messages, (re->message_count + 1) * sizeof(PetkaMessage));
                if (!msgs) return;
                memmove(msgs + m + 2, msgs + m + 1, (re->message_count - m - 1) * sizeof(PetkaMessage));
                msgs[m + 1] = (PetkaMessage){.obj_id = kFixes[f].add_to, .opcode = kFixes[f].add_op,
                                             .arg1 = kFixes[f].add_arg1, .arg2 = PETKA_NONE, .arg3 = PETKA_NONE};
                re->messages = msgs;
                re->message_count++;
                break;
            }
        }
    }
}

void scriptfix_state(Game *g) {
    if (g->game != 1) return;
    for (size_t f = 0; f < sizeof(kFixes) / sizeof(kFixes[0]); f++) {
        if (kFixes[f].part != g->part_number || kFixes[f].add_op != PETKA_OP_SET) continue;
        PetkaObjectState *st = petka_scriptvm_state(g->vm, kFixes[f].add_to);
        if (st && !st->visible && st->resource == kFixes[f].stale) st->resource = kFixes[f].add_arg1;
    }
}
