// host.c
//
// EN: The script VM's host: what the launcher does for the script's messages and room changes.
// RU: Хост машины скрипта: что лаунчер делает по сообщениям скрипта и при смене комнаты.
#include "game.h"

void host_room_changed(void *user, uint16_t prev_id, uint16_t room_id) {
    Game *g = (Game *)user;
    // A place picked on the map: the map is done with.
    map_leave(g);
    const PetkaObject *room = petka_script_find(g->script, room_id);
    const PetkaObject *prev = prev_id != PETKA_NONE ? petka_script_find(g->script, prev_id) : NULL;
    if (!room) return;
    load_perspective(g, room->name);
    load_mesh(g);
    g->camera_snap = 1;
    // (Both heroes were stopped by the VM before the room changed.)
    g->walk[0].active = g->walk[1].active = 0;
    // The clips of the room that is left go, and their sounds with them:
    // nothing of it is heard in the next one. (The voice is the dialogue's
    // and may well go on across a door.)
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g->sounds[i].frameCount > 0) StopSound(g->sounds[i]);
        g->sound_paused[i] = 0;
    }

    const PetkaRoom *section = g->topo ? petka_topology_find_room(g->topo, room->name) : NULL;
    const PetkaObject *entry = NULL;
    for (uint32_t i = 0; section && i < section->exit_count && !entry; i++)
        if (prev && strcmp(section->exits[i].target, prev->name) == 0)
            entry = petka_script_find_name(g->script, section->exits[i].description);
    // Arriving from somewhere with no exit of its own (the map, a cutscene):
    // the exe falls back to the first entry point it finds.
    for (uint32_t i = 0; section && i < section->exit_count && !entry; i++)
        entry = petka_script_find_name(g->script, section->exits[i].description);
    if (!entry) return;

    const PetkaObjectState *spot = petka_scriptvm_state(g->vm, entry->id);
    PetkaObjectState *petka = petka_scriptvm_state(g->vm, PETKA_ID_PETKA);
    PetkaObjectState *chapayev = petka_scriptvm_state(g->vm, PETKA_ID_CHAPAYEV);
    if (!spot || spot->walk_x == -1) return;
    if (petka) { petka->x = spot->walk_x; petka->y = spot->walk_y; petka->busy = 0; }
    if (chapayev) { chapayev->x = spot->walk_x; chapayev->y = spot->walk_y - 2; chapayev->busy = 0; }
}

void host_message(void *user, PetkaScriptVM *vm, const PetkaObject *obj,
                         const PetkaQueued *q, int reacted) {
    Game *g = (Game *)user;
    const PetkaMessage *m = &q->msg;
    // A broadcast reaches every object; only the reactions are worth logging.
    int noise = !reacted && (m->opcode == PETKA_OP_INITBG || m->opcode == PETKA_OP_LEAVEBG ||
                             m->opcode == PETKA_OP_TOTALINIT || m->opcode == PETKA_OP_END ||
                             m->opcode == PETKA_OP_HALF);
    if (g->trace && !noise)
        fprintf(stderr, "%-10s -> %u[%s] %d %d %d%s\n", petka_opcode_name(m->opcode), obj->id,
                utf8_name(obj->name), (int16_t)m->arg1, (int16_t)m->arg2, (int16_t)m->arg3,
                reacted ? "  (reaction)" : "");
    switch (m->opcode) {
    case PETKA_OP_SET:
    case PETKA_OP_PLAY:
        // PLAY starts the clip over; so does SET with 1 as its second argument.
        if (m->opcode == PETKA_OP_PLAY || m->arg2 == 1) g->visuals[obj_index(g, obj)].restart = 1;
        break;
    case PETKA_OP_SETSEQ:
        // A sequence takes the screen from whatever has it. The map or the
        // panel may be up when a clip that was still playing sets one off;
        // left open, they would keep the sequence from ever being shown.
        map_leave(g);
        sequence_sounds(g, 1);
        g->seq_room = obj->id;
        if (g->trace) fprintf(stderr, "seq: %u is up\n", obj->id);
        break;
    case PETKA_OP_GOTO:
        // Going to a room ends the sequence that is up, as ENDSEQ does: the
        // exe's room change starts by stopping the screen that has the
        // display (ScummVM's InterfaceMain::loadRoom, _currInterface->stop()).
        // The first game's scripts say ENDSEQ next to their GOTO all the
        // same; the second's intro to part 2 (Seq - INTRO - VICH1) does not,
        // and its sequence went on for ever over Broadway. The map is such
        // a screen too, and is left as the map is: a place picked on it may
        // be the very room it was put up from (the second game's exits to
        // the map), and no room change would then take it down.
        if (g->map_open) { map_leave(g); break; }
        if (g->seq_room == PETKA_NONE) break;
        // fall through
    case PETKA_OP_ENDSEQ:
        if (g->seq_room != PETKA_NONE) sequence_sounds(g, 0);
        g->seq_room = PETKA_NONE;
        g->camera_snap = 1;
        break;
    case PETKA_OP_CONTINUE:
        if (g->paused) {
            g->paused = 0;
            g->talking = 1;
            next_phrase(g);
            // A pause can be the dialogue's last instruction, and then it is
            // over here, in the middle of the script's turn. The script must
            // hear of it now: the messages still waiting may start another
            // dialogue (in part 2 the lieutenant's USERMSG reaction goes
            // straight on to TALK), and an end told a frame later would be
            // taken for that one's.
            if (g->dialogue_done) {
                g->dialogue_done = 0;
                uint16_t talker = g->talker;
                g->talker = PETKA_NONE;
                petka_scriptvm_dialogue_ended(vm, talker);
            }
        }
        break;
    case PETKA_OP_SYSTEM:
        // 242 ends the game (the last of part 3's credits). The exe quits;
        // the launcher goes back to its start-up menu instead.
        if (m->arg1 == 242) g->to_menu = 1;
        break;
    case PETKA_OP_AVI:
        play_film(g, g->qrc[m->arg1]);
        break;
    case PETKA_OP_TOMAP:
        // The second game's exits to places reached by the map alone, and
        // some of its conversations' ends: the map command, as M or the
        // case's button gives it (ScummVM's kToMap, toggleMapInterface) --
        // in the frame's own time, not in the middle of the script's turn.
        g->map_request = 1;
        break;
    case PETKA_OP_PART:
        // The whole game is taken down and built again for the new part
        // (sub_412040); that waits for the top of the next frame.
        g->switch_part = m->arg1;
        g->switch_chapter = 1;
        break;
    case PETKA_OP_CHAPTER:
        chapter_open(g, m->arg1);
        break;
    case PETKA_OP_SHOWDESCRIPTION: {
        // The text is the object's line in [all] of INVNTR.TXT, by its
        // script name; the message's number goes with it to the screen.
        size_t len = 0;
        const uint8_t *ini = part_file(g, g->part, "INVNTR.TXT", &len);
        char text[1024];
        if (ini && petka_ini_get(ini, len, "all", obj->name, text, sizeof(text)) && text[0])
            cp1251_to_utf8(text, g->desc, sizeof(g->desc));
        else
            snprintf(g->desc, sizeof(g->desc), "%s", display_name(g, obj));
        g->desc_frame = m->arg1;
        break;
    }
    case PETKA_OP_CURSOR:
        // CURSOR with no clip puts the cursor back on its first action.
        if (m->arg1 == PETKA_NONE) g->verb = 0;
        break;
    default:
        break;
    }
}
