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
// `via`: an object whose reaction to the player passes the action on to
// this one, and gets the line too (0 for none).
typedef struct {
    uint8_t part;
    uint16_t obj;
    uint32_t key;
    uint8_t status;
    uint8_t instead;
    uint16_t then[3];
    uint16_t via;
} Restored;

static const Restored kRestored[] = {
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

// The second game's, found the same way (its Reloaded edition, 2016): of the
// reactions whose object's own dialogue the exe leaves unsaid, those whose
// line it says nowhere else -- not in another status of the object, where
// no reaction leaves the dialogue to be said, nor through a talk of the
// reaction's own -- and that Reloaded says for the same object and action.
// Left out: the beekeeper's formula (a talk of its own; Reloaded says it
// only once he waits for it, status 2, where the exe says it too), the
// radiation sign (Reloaded says it before the red button, status 0, as the
// exe does), the laser's halved glasses (Reloaded says them before the
// handlebar is up, status 0, where the exe says them too), the fly on the
// bolts (Reloaded gives Chapayev's line to TAKE: kAfter2).
static const Restored kRestored2[] = {
    // The crane with the stick: the exe's "Окей!" is the first way down
    // alone (status 0); Reloaded says it each time, the well walked to too.
    {1, 145, 0x1, 1, 0, {0}, 142}, // Окей!
    {1, 100, 0x1, 0, 1}, // Спрятаться в шкаф? (the wardrobe opens by its door)
    {1, 100, 0x1, 2, 1}, // Спрятаться в шкаф?
    {1, 317, 0x6, 255}, // А вот это, пожалуй, возьмем.
    {1, 393, 0x6, 0}, // А вот это мой размерчик.
    {1, 393, 0x6, 1}, // А вот это мой размерчик.
};

// Phrases of other dialogues the second game's Reloaded says after an
// object's own dialogue for the player's action, where the object has no
// reaction to it (restored_after()): the part, the object, the opcode, the
// phrase table's indices.
static const struct {
    uint8_t part;
    uint16_t obj, op;
    uint16_t then[3];
} kAfter2[] = {
    // The fly on the bolts, taken: Petka's "Я бы с удовольствием убрал ее
    // оттуда, но она прикручена здоровыми болтами." and Chapayev's "Что
    // мне, зубами эти болты перекусывать?" (his dialogue for being sent to
    // it, which the exe never says; Reloaded has him go and hit it silent).
    {1, 346, PETKA_OP_TAKE, {1174}},
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

// The lines restored are the first game's, by its parts and objects.
int restored_phrases(const Game *g, uint16_t obj_id, uint16_t *out, int max) {
    if (g->game != 1) return 0;
    for (size_t i = 0; i < sizeof(kPhrases) / sizeof(kPhrases[0]); i++) {
        if (kPhrases[i].part != g->part_number || kPhrases[i].obj != obj_id) continue;
        int n = 0;
        for (int k = 0; k < 3 && kPhrases[i].phrases[k] && n < max; k++) out[n++] = kPhrases[i].phrases[k];
        return n;
    }
    return 0;
}

const PetkaDialogueVerb *restored_split(const Game *g, uint16_t obj_id, int *first) {
    if (g->game != 1) return NULL;
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

void restored_after(Game *g, uint16_t obj_id) {
    if (!g->opt_restored || g->game != 2 || obj_id != g->acted_obj) return;
    for (size_t i = 0; i < sizeof(kAfter2) / sizeof(kAfter2[0]); i++)
        if (kAfter2[i].part == g->part_number && kAfter2[i].obj == obj_id && kAfter2[i].op == g->acted_op) {
            g->acted_obj = PETKA_NONE;
            g->then_count = g->then_pos = 0;
            for (int k = 0; k < 3 && kAfter2[i].then[k]; k++) g->then_phrase[g->then_count++] = kAfter2[i].then[k];
            return;
        }
}

int host_restore_dialogue(void *user, uint16_t obj_id, uint32_t key, uint8_t status) {
    Game *g = (Game *)user;
    if (!g->opt_restored || g->acted_obj == PETKA_NONE) return 0;
    // Only for the player's own action (or one an object passed on, `via`),
    // and once.
    int own = obj_id == g->acted_obj && (key & 0xFFFF) == g->acted_op;
    const Restored *table = g->game == 2 ? kRestored2 : kRestored;
    size_t count = g->game == 2 ? sizeof(kRestored2) / sizeof(kRestored2[0]) : sizeof(kRestored) / sizeof(kRestored[0]);
    for (size_t i = 0; i < count; i++)
        if (table[i].part == g->part_number && table[i].obj == obj_id && table[i].key == key &&
            table[i].status == status && (own || (table[i].via && table[i].via == g->acted_obj))) {
            g->acted_obj = PETKA_NONE;
            g->then_count = g->then_pos = 0;
            for (int k = 0; k < 3 && table[i].then[k]; k++) g->then_phrase[g->then_count++] = table[i].then[k];
            return table[i].instead ? 2 : 1;
        }
    return 0;
}
