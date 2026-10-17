// restored.c
//
// EN: Restored dialogues: the lines the exe never says that the Reloaded edition put back.
// RU: Восстановленные диалоги: реплики, которые exe не произносит, а переиздание Reloaded вернуло.
#include "game.h"

// The reactions whose object's own dialogue the exe leaves unsaid, among
// them those the Reloaded edition's script does say: the part, the object,
// the message key (opcode, or (sender << 16) | OBJECTUSE) and the
// reaction's status (0xFF any). Found by comparing the scripts: Reloaded
// says the line for the same object (its clip's name) and the same action.
// Left out: reactions that start with an IF or a CHECK, as they may not
// run; lines Reloaded gives another object or action (DIALOGUE.FIX ties
// the safe's "I won't open the safe even with the code" to the toilet
// handle); reactions that pass the action on to an object with no
// reaction to it, whose stock reply would follow the line; and lines the
// exe does say, in another of the object's statuses or through another of
// its reactions -- Anka's door: in status 1, after the bath is lit, USE
// sends USERMSG 2, whose reaction has the DIALOG; put back in status 0 the
// talk of persuading Anka came before the bath; reactions that start a talk
// of their own (TALK to a dialogue's holder: the bath's stove, the cow, the
// cartridge, the safe, the twig -- the line came and then the exe's own);
// and lines Reloaded says under a state of its story that the object's
// status does not tell (the keyhole, the bush, the safe's USE, the
// cartridge's TAKE; in part 2 the net in the shop, the church's brick and
// its hole, the rope at the jetty; in part 3 the fireplace's handle, whose
// "Неплохо." Reloaded says after the pull, and only while the lamp is not
// turned; the bar's remote, whose "Нет. Нельзя." is the barman's while he
// is there, and status 1 is the remote taken once he has gone; the lift in
// control room 1, whose "Здесь нет пульта управления подъемником." Reloaded
// says while it will not go, and status 1 is the lift going; the
// manipulator's desk, "Пульт сейчас занят." while the professor is at it,
// and status 1 is Petka at the stick). The comment is
// the dialogue's first line. `instead`: Reloaded only says the line there,
// and the reaction's work is done another way -- the line is said in its
// place (the wardrobe opens by its door, which passes USE on to it). `then`:
// phrases of other dialogues Reloaded has said after it, before the
// reaction, where without them the line and the reaction disagree (the
// phrase table's indices, 0 for none). A line that tells of what the action
// has done (the fireplace's lamp, "I turned it, though I did not tear it off")
// is not here but a DIALOG that scriptfix.c puts at the reaction's end.
static const struct {
    uint8_t part;
    uint16_t obj;
    uint32_t key;
    uint8_t status;
    uint8_t instead;
    uint16_t then[3];
} kRestored[] = {
    {1, 89, 0x6, 1}, // Ой, что это там на дне блестит?
    {1, 93, 0x1, 0, 1}, // Спрятаться в шкаф?
    {1, 93, 0x1, 2, 1}, // Спрятаться в шкаф?
    {1, 168, 0x6, 0}, // А вот эти детали нам точно пригодятся.
    {1, 176, 0x6, 0}, // Пригодятся.
    // ...and then takes the skin: Reloaded has Petka go on "Вам надо, вы и
    // берите." (the dialogue says only its first line), Chapayev "Бери сам,
    // Петька" (dialogue 29) and Petka "Да, ладно вам..., я ж шучу..." (60).
    {1, 232, 0x6, 255, 0, {1064, 1682, 2659}}, // Не, Василь Иваныч, я теперь и пальцем ее не коснусь...
    {1, 288, 0x6, 0}, // Возьмем! Халява - наш стиль жизни!
    {1, 289, 0x6, 255}, // Это весла Фурманова.
    {1, 325, 0x6, 255}, // Он нам пригодится. Что-нибудь да откроем.
    {2, 89, 0x1, 255}, // В этой бочке сжигают мусор...
    {2, 262, 0x6, 255}, // Вот это, то что надо!
    {2, 319, 0x6, 1}, // Пусть висит.
    {2, 485, 0x1, 255}, // По идее, туда надо чего-то залить.
    {3, 55, 0x1, 1}, // Можно было бы попробовать туда залезть, если бы не решетка.
    {3, 181, 0x1, 2}, // Если кто-то где-то сделал кнопки, значит это для чего-то нужно...
    {3, 297, 0x6, 255}, // Берем.
    {3, 343, 0xF001A, 255}, // Хорошая мысль, Петька!
    {3, 344, 0xE001A, 255}, // Василь Иваныч, можно вас попросить?
    {3, 348, 0x1001A, 255}, // Берем!
    {3, 349, 0x1001A, 255}, // Почему бы и нет?
};

// The dialogues said in two parts round the action (restored.h): the part,
// the object, the message key of its own dialogue, how many phrases first.
static const struct {
    uint8_t part;
    uint16_t obj;
    uint32_t key;
    int first;
} kSplit[] = {
    // Part 2, the rope at the jetty, cut with the sword: Petka's "Вот так-то
    // лучше будет... Оп-ля!" as he gets to it, Chapayev's "Острый, зараза!"
    // once it is cut -- as Reloaded has it, not both before the cut.
    {2, 653, 0x19001A, 1},
    // Part 1, the bath's keyhole, the syringe with the bees: the talk after
    // the anarchists' sequence, all of it (scriptfix.c).
    {1, 128, 0x28001A, 0},
    // Part 2, the church's hole and its brick: the talk once the crypt has
    // opened (scriptfix.c).
    {2, 549, 0x15001A, 0},
};

// Phrases said where the script says nothing (restored.h): the part, the
// object the DIALOG goes to, the phrase table's indices (0 for none).
static const struct {
    uint8_t part;
    uint16_t obj;
    uint16_t phrases[3];
} kPhrases[] = {
    // Part 3, the White HQ's fireplace lamp, Chapayev sent to it: Petka asks
    // him first, "Василь Иванович, а вы не могли бы попробовать оторвать
    // отсюда эту лампу?" (the lamp's USE dialogue) -- as Reloaded has it --
    // and then he goes, turns it and says his own.
    {3, 63, {154}},
};

int restored_phrases(const Game *g, uint16_t obj_id, uint16_t *out, int max) {
    for (size_t i = 0; i < sizeof(kPhrases) / sizeof(kPhrases[0]); i++) {
        if (kPhrases[i].part != g->part_number || kPhrases[i].obj != obj_id) continue;
        int n = 0;
        for (int k = 0; k < 3 && kPhrases[i].phrases[k] && n < max; k++) out[n++] = kPhrases[i].phrases[k];
        return n;
    }
    return 0;
}

const PetkaDialogueVerb *restored_split(const Game *g, uint16_t obj_id, int *first) {
    for (size_t i = 0; i < sizeof(kSplit) / sizeof(kSplit[0]); i++)
        if (kSplit[i].part == g->part_number && kSplit[i].obj == obj_id) {
            *first = kSplit[i].first;
            return find_dialogue(g, obj_id, kSplit[i].key);
        }
    return NULL;
}

void restored_player_action(Game *g, uint16_t obj, uint16_t op) {
    g->acted_obj = obj;
    g->acted_op = op;
}

int host_restore_dialogue(void *user, uint16_t obj_id, uint32_t key, uint8_t status) {
    Game *g = (Game *)user;
    if (!g->opt_restored) return 0;
    // Only for the player's own action, and once.
    if (obj_id != g->acted_obj || (key & 0xFFFF) != g->acted_op) return 0;
    for (size_t i = 0; i < sizeof(kRestored) / sizeof(kRestored[0]); i++)
        if (kRestored[i].part == g->part_number && kRestored[i].obj == obj_id && kRestored[i].key == key &&
            kRestored[i].status == status) {
            g->acted_obj = PETKA_NONE;
            g->then_count = g->then_pos = 0;
            for (int k = 0; k < 3 && kRestored[i].then[k]; k++) g->then_phrase[g->then_count++] = kRestored[i].then[k];
            return kRestored[i].instead ? 2 : 1;
        }
    return 0;
}
