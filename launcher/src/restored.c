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
// talk of persuading Anka came before the bath. The comment is
// the dialogue's first line. `instead`: Reloaded only says the line there,
// and the reaction's work is done another way -- the line is said in its
// place (the wardrobe opens by its door, which passes USE on to it).
static const struct {
    uint8_t part;
    uint16_t obj;
    uint32_t key;
    uint8_t status;
    uint8_t instead;
} kRestored[] = {
    {1, 89, 0x6, 1}, // Ой, что это там на дне блестит?
    {1, 93, 0x1, 0, 1}, // Спрятаться в шкаф?
    {1, 93, 0x1, 2, 1}, // Спрятаться в шкаф?
    {1, 128, 0x28001A, 0}, // Ай да Петька!
    {1, 136, 0x1001A, 0}, // Василий Иванович, огоньку не найдется?
    {1, 168, 0x6, 0}, // А вот эти детали нам точно пригодятся.
    {1, 176, 0x6, 0}, // Пригодятся.
    {1, 231, 0x1001A, 1}, // А во что я ее по-твоему доить буду?
    {1, 232, 0x6, 255}, // Не, Василь Иваныч, я теперь и пальцем ее не коснусь...
    {1, 288, 0x6, 0}, // Возьмем! Халява - наш стиль жизни!
    {1, 289, 0x6, 255}, // Это весла Фурманова.
    {1, 319, 0x6, 1}, // Насыпь слишком крутая и высокая, так просто его не достать.
    {1, 319, 0x4, 1}, // Гляди-ка, Василь Иваныч - патрон!
    {1, 323, 0x1001A, 2}, // Значит шифр, говоришь!
    {1, 325, 0x6, 255}, // Он нам пригодится. Что-нибудь да откроем.
    {1, 326, 0x1001A, 2}, // Я тебе, Петька, не медвежатник.
    {1, 326, 0x1, 2}, // Как бы нам его вскрыть, а Василь Иваныч?
    {1, 403, 0x24001A, 0}, // Прихватим прутик - отхлещем кого-нибудь при случае...
    {1, 425, 0x1, 0}, // Кустик, конечно, ничего...
    {2, 89, 0x1, 255}, // В этой бочке сжигают мусор...
    {2, 262, 0x6, 255}, // Вот это, то что надо!
    {2, 319, 0x6, 1}, // Пусть висит.
    {2, 401, 0x6, 1}, // За это надо платить.
    {2, 485, 0x1, 255}, // По идее, туда надо чего-то залить.
    {2, 549, 0x15001A, 0}, // Ой, Василь Иваныч, слышь, скрежет-то какой - будто под полом тащут чего...
    {2, 550, 0x1, 0}, // Лучше его не трогать - мало ли что...
    {2, 653, 0x19001A, 0}, // Вот так-то лучше будет...  Оп-ля!
    {2, 653, 0x19001A, 1}, // Вот так-то лучше будет...  Оп-ля!
    {3, 55, 0x1, 1}, // Можно было бы попробовать туда залезть, если бы не решетка.
    {3, 60, 0x1, 255}, // Неплохо.
    {3, 63, 0x1001A, 0}, // Оторвать не получилось, но свернул я ее по-моему неплохо, а Петька?
    {3, 181, 0x1, 2}, // Если кто-то где-то сделал кнопки, значит это для чего-то нужно...
    {3, 222, 0x6, 1}, // Нет. Нельзя.
    {3, 260, 0x1, 1}, // Пульт сейчас занят.
    {3, 297, 0x6, 255}, // Берем.
    {3, 315, 0x1, 1}, // Здесь нет пульта управления подъемником.
    {3, 343, 0xF001A, 255}, // Хорошая мысль, Петька!
    {3, 344, 0xE001A, 255}, // Василь Иваныч, можно вас попросить?
    {3, 348, 0x1001A, 255}, // Берем!
    {3, 349, 0x1001A, 255}, // Почему бы и нет?
};

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
            return kRestored[i].instead ? 2 : 1;
        }
    return 0;
}
