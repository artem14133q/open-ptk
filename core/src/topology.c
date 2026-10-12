#include "petka/topology.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *dup_trimmed(const char *start, const char *end) {
    while (start < end && (unsigned char)*start <= ' ') start++;
    while (end > start && (unsigned char)*(end - 1) <= ' ') end--;
    size_t n = (size_t)(end - start);
    char *s = (char *)malloc(n + 1);
    memcpy(s, start, n);
    s[n] = 0;
    return s;
}

static int ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static PetkaRoom *find_or_add_room(PetkaTopology *topo, const char *name) {
    for (uint32_t i = 0; i < topo->room_count; i++)
        if (strcmp(topo->rooms[i].name, name) == 0) return &topo->rooms[i];
    topo->rooms = (PetkaRoom *)realloc(topo->rooms, sizeof(PetkaRoom) * (topo->room_count + 1));
    PetkaRoom *r = &topo->rooms[topo->room_count++];
    r->name = strdup(name);
    r->exits = NULL;
    r->exit_count = 0;
    return r;
}

PetkaTopology *petka_topology_parse(const uint8_t *data, size_t len) {
    PetkaTopology *topo = (PetkaTopology *)calloc(1, sizeof(PetkaTopology));

    int in_settings = 0;
    PetkaRoom *current_room = NULL;

    const char *p = (const char *)data;
    const char *end = (const char *)data + len;
    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        const char *trim_end = line_end;
        if (trim_end > p && trim_end[-1] == '\r') trim_end--;

        const char *ls = p;
        while (ls < trim_end && (unsigned char)*ls <= ' ') ls++;

        if (ls >= trim_end || *ls == ';') {
            // blank or comment
        } else if (*ls == '[') {
            const char *close = memchr(ls, ']', (size_t)(trim_end - ls));
            if (close) {
                char *name = dup_trimmed(ls + 1, close);
                in_settings = ci_equal(name, "Settings");
                current_room = in_settings ? NULL : find_or_add_room(topo, name);
                free(name);
            }
        } else {
            const char *eq = memchr(ls, '=', (size_t)(trim_end - ls));
            if (eq) {
                char *key = dup_trimmed(ls, eq);
                char *value = dup_trimmed(eq + 1, trim_end);
                if (in_settings) {
                    if (ci_equal(key, "StartRoom")) {
                        free(topo->start_room);
                        topo->start_room = strdup(value);
                    }
                    // else: per-room scale params, not parsed (see topology.h)
                } else if (current_room) {
                    current_room->exits = (PetkaExit *)realloc(
                        current_room->exits, sizeof(PetkaExit) * (current_room->exit_count + 1));
                    PetkaExit *ex = &current_room->exits[current_room->exit_count++];
                    ex->target = key;
                    ex->description = value;
                    key = NULL;
                    value = NULL;
                }
                free(key);
                free(value);
            }
        }

        p = (line_end < end) ? line_end + 1 : end;
    }

    return topo;
}

void petka_topology_close(PetkaTopology *topo) {
    if (!topo) return;
    for (uint32_t i = 0; i < topo->room_count; i++) {
        PetkaRoom *r = &topo->rooms[i];
        for (uint32_t j = 0; j < r->exit_count; j++) {
            free(r->exits[j].target);
            free(r->exits[j].description);
        }
        free(r->exits);
        free(r->name);
    }
    free(topo->rooms);
    free(topo->start_room);
    free(topo);
}

const PetkaRoom *petka_topology_find_room(const PetkaTopology *topo, const char *name) {
    if (!topo) return NULL;
    for (uint32_t i = 0; i < topo->room_count; i++)
        if (strcmp(topo->rooms[i].name, name) == 0) return &topo->rooms[i];
    return NULL;
}
