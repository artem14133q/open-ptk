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

void scriptfix_script(Game *g) {
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
    for (size_t f = 0; f < sizeof(kFixes) / sizeof(kFixes[0]); f++) {
        if (kFixes[f].part != g->part_number || kFixes[f].add_op != PETKA_OP_SET) continue;
        PetkaObjectState *st = petka_scriptvm_state(g->vm, kFixes[f].add_to);
        if (st && !st->visible && st->resource == kFixes[f].stale) st->resource = kFixes[f].add_arg1;
    }
}
