// Host-only dev tool: parse a PART_N's BGS.INI from a store (usually
// MAIN.STR) and print the room graph, to validate the parser against real
// data and sanity-check that every exit target resolves to a known room.
#include "petka/store.h"
#include "petka/topology.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <store.str> <part_prefix e.g. PART_1>\n", argv[0]);
        return 1;
    }
    PetkaStore *store = petka_store_open(argv[1]);
    if (!store) { fprintf(stderr, "open failed\n"); return 1; }

    char name[256];
    snprintf(name, sizeof(name), "%s\\BGS.INI", argv[2]);
    const PetkaEntry *e = petka_store_find(store, name);
    if (!e) { fprintf(stderr, "entry not found: %s\n", name); return 1; }

    size_t len = 0;
    const uint8_t *bytes = petka_store_entry_data(store, e, &len);
    PetkaTopology *topo = petka_topology_parse(bytes, len);

    printf("start_room: %s\n", topo->start_room ? topo->start_room : "(none)");
    printf("rooms: %u\n\n", topo->room_count);

    int dangling = 0;
    for (uint32_t i = 0; i < topo->room_count; i++) {
        PetkaRoom *r = &topo->rooms[i];
        printf("[%s] (%u exits)\n", r->name, r->exit_count);
        for (uint32_t j = 0; j < r->exit_count; j++) {
            const PetkaRoom *target = petka_topology_find_room(topo, r->exits[j].target);
            printf("  -> %-30s %s%s\n", r->exits[j].target, r->exits[j].description,
                   target ? "" : "   !! dangling (no such room section)");
            if (!target) dangling++;
        }
    }
    printf("\ndangling exit targets: %d\n", dangling);

    petka_topology_close(topo);
    petka_store_close(store);
    return 0;
}
