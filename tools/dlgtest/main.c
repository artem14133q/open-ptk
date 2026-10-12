// Host-only dev tool: parse a PART_N's DIALOGUE.FIX/.LOD pair from a store
// (usually MAIN.STR) and walk every topic/verb entry point, printing the
// resulting phrase text, to validate the parser + VM against real data.
#include "petka/dialogue.h"
#include "petka/store.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <store.str> <part_prefix e.g. PART_1>\n", argv[0]);
        return 1;
    }
    const char *store_path = argv[1];
    const char *part = argv[2];

    PetkaStore *store = petka_store_open(store_path);
    if (!store) { fprintf(stderr, "open failed\n"); return 1; }

    char lod_name[256], fix_name[256];
    snprintf(lod_name, sizeof(lod_name), "%s\\DIALOGUE.LOD", part);
    snprintf(fix_name, sizeof(fix_name), "%s\\DIALOGUE.FIX", part);

    const PetkaEntry *lod_e = petka_store_find(store, lod_name);
    const PetkaEntry *fix_e = petka_store_find(store, fix_name);
    if (!lod_e || !fix_e) { fprintf(stderr, "entries not found\n"); return 1; }

    size_t lod_len = 0, fix_len = 0;
    const uint8_t *lod_bytes = petka_store_entry_data(store, lod_e, &lod_len);
    const uint8_t *fix_bytes = petka_store_entry_data(store, fix_e, &fix_len);

    PetkaPhraseTable *phrases = petka_phrasetable_open(lod_bytes, lod_len);
    PetkaDialogueTree *tree = petka_dialoguetree_open(fix_bytes, fix_len);
    if (!phrases) { fprintf(stderr, "phrase table parse failed\n"); return 1; }
    if (!tree) { fprintf(stderr, "dialogue tree parse failed\n"); return 1; }

    printf("phrases: %u, topics: %u, bytecode words: %u\n", phrases->count, tree->topic_count, tree->code_count);

    int shown = 0;
    for (uint32_t i = 0; i < tree->topic_count && shown < 5; i++) {
        PetkaDialogueTopic *topic = &tree->topics[i];
        printf("topic #%u (id=%u, %u verbs)\n", i, topic->topic_id, topic->verb_count);
        for (uint32_t j = 0; j < topic->verb_count; j++) {
            PetkaDialogueVerb *verb = &topic->verbs[j];
            for (uint32_t k = 0; k < verb->entry_count; k++) {
                uint32_t off = verb->entries[k].bytecode_offset;
                printf("  verb=%u entry[%u] @word%u:\n", verb->verb_id, k, off);
                PetkaDialogueVM vm;
                petka_dialoguevm_start(&vm, tree, off);
                uint32_t phrase_idx;
                int steps = 0;
                while (petka_dialoguevm_next(&vm, &phrase_idx) && steps < 10) {
                    if (phrase_idx < phrases->count)
                        printf("    [%u] %s\n", phrase_idx, phrases->phrases[phrase_idx].text);
                    else
                        printf("    [%u] <out of range>\n", phrase_idx);
                    steps++;
                }
            }
        }
        shown++;
    }

    petka_dialoguetree_close(tree);
    petka_phrasetable_close(phrases);
    petka_store_close(store);
    return 0;
}
