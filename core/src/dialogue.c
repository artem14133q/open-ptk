#include "petka/dialogue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// ---- DIALOGUE.LOD ----

PetkaPhraseTable *petka_phrasetable_open(const uint8_t *data, size_t len) {
    if (len < 8) return NULL;
    uint32_t count = read_u32le(data);
    // offset 4..7 is a second header field, always 0 in samples seen -- skip it.
    size_t table_bytes = (size_t)count * 24;
    if (8 + table_bytes > len) return NULL;

    PetkaPhraseTable *table = (PetkaPhraseTable *)calloc(1, sizeof(PetkaPhraseTable));
    table->count = count;
    table->phrases = (PetkaPhrase *)calloc(count, sizeof(PetkaPhrase));

    size_t pos = 8;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(table->phrases[i].wav_name, data + pos, 16);
        table->phrases[i].wav_name[16] = 0;
        pos += 24; // skip wav_name(16) + 2 ignored u32 fields
    }

    for (uint32_t i = 0; i < count; i++) {
        size_t start = pos;
        while (pos < len && data[pos] != 0) pos++;
        if (pos >= len) { petka_phrasetable_close(table); return NULL; }
        size_t n = pos - start;
        table->phrases[i].text = (char *)malloc(n + 1);
        memcpy(table->phrases[i].text, data + start, n);
        table->phrases[i].text[n] = 0;
        pos++; // skip NUL
    }

    return table;
}

void petka_phrasetable_close(PetkaPhraseTable *table) {
    if (!table) return;
    if (table->phrases) {
        for (uint32_t i = 0; i < table->count; i++) free(table->phrases[i].text);
        free(table->phrases);
    }
    free(table);
}

// ---- DIALOGUE.FIX ----

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    int error;
} Reader;

static uint32_t rd(Reader *r) {
    if (r->pos + 4 > r->len) { r->error = 1; return 0; }
    uint32_t v = read_u32le(r->data + r->pos);
    r->pos += 4;
    return v;
}

PetkaDialogueTree *petka_dialoguetree_open(const uint8_t *data, size_t len) {
    Reader r = {data, len, 0, 0};

    uint32_t topic_count = rd(&r);
    if (r.error) return NULL;

    // Topic headers: flat block of {id, n_sub, _ignored}.
    typedef struct { uint32_t id, n_sub; } TH;
    TH *theaders = (TH *)malloc(sizeof(TH) * topic_count);
    for (uint32_t i = 0; i < topic_count; i++) {
        theaders[i].id = rd(&r);
        theaders[i].n_sub = rd(&r);
        rd(&r); // ignored (overwritten pointer slot on-disk)
    }
    if (r.error) { free(theaders); return NULL; }

    PetkaDialogueTree *tree = (PetkaDialogueTree *)calloc(1, sizeof(PetkaDialogueTree));
    tree->topic_count = topic_count;
    tree->topics = (PetkaDialogueTopic *)calloc(topic_count, sizeof(PetkaDialogueTopic));

    for (uint32_t i = 0; i < topic_count; i++) {
        PetkaDialogueTopic *topic = &tree->topics[i];
        topic->topic_id = theaders[i].id;
        topic->verb_count = theaders[i].n_sub;
        topic->verbs = (PetkaDialogueVerb *)calloc(topic->verb_count, sizeof(PetkaDialogueVerb));

        // Sub (verb) headers: flat block of {verb_id, n_subsub, data, _ignored}.
        typedef struct { uint32_t verb_id, n_subsub, data; } SH;
        SH *sheaders = (SH *)malloc(sizeof(SH) * topic->verb_count);
        for (uint32_t j = 0; j < topic->verb_count; j++) {
            sheaders[j].verb_id = rd(&r);
            sheaders[j].n_subsub = rd(&r);
            sheaders[j].data = rd(&r);
            rd(&r); // ignored
        }

        for (uint32_t j = 0; j < topic->verb_count; j++) {
            PetkaDialogueVerb *verb = &topic->verbs[j];
            verb->verb_id = sheaders[j].verb_id;
            verb->data = sheaders[j].data;
            verb->entry_count = sheaders[j].n_subsub;
            verb->entries = (PetkaDialogueEntry *)calloc(verb->entry_count, sizeof(PetkaDialogueEntry));

            // Subsub (entry point) headers: flat block of {bytecode_offset, n_extra, _ignored}.
            typedef struct { uint32_t off, n_extra; } TT;
            TT *theads = (TT *)malloc(sizeof(TT) * verb->entry_count);
            for (uint32_t k = 0; k < verb->entry_count; k++) {
                theads[k].off = rd(&r);
                theads[k].n_extra = rd(&r);
                rd(&r); // ignored
            }
            for (uint32_t k = 0; k < verb->entry_count; k++) {
                PetkaDialogueEntry *entry = &verb->entries[k];
                entry->bytecode_offset = theads[k].off;
                entry->extra_count = theads[k].n_extra;
                entry->extra = (uint32_t *)malloc(sizeof(uint32_t) * (entry->extra_count ? entry->extra_count : 1));
                for (uint32_t m = 0; m < entry->extra_count; m++) entry->extra[m] = rd(&r);
            }
            free(theads);
        }
        free(sheaders);
    }
    free(theaders);

    if (r.error) { petka_dialoguetree_close(tree); return NULL; }

    tree->code_count = rd(&r);
    if (r.error) { petka_dialoguetree_close(tree); return NULL; }
    tree->code = (uint32_t *)malloc(sizeof(uint32_t) * (tree->code_count ? tree->code_count : 1));
    for (uint32_t i = 0; i < tree->code_count; i++) tree->code[i] = rd(&r);

    if (r.error || r.pos != len) { // exact-EOF is our validity check, per docs/str-format.md
        petka_dialoguetree_close(tree);
        return NULL;
    }
    return tree;
}

void petka_dialoguetree_close(PetkaDialogueTree *tree) {
    if (!tree) return;
    if (tree->topics) {
        for (uint32_t i = 0; i < tree->topic_count; i++) {
            PetkaDialogueTopic *topic = &tree->topics[i];
            if (!topic->verbs) continue;
            for (uint32_t j = 0; j < topic->verb_count; j++) {
                PetkaDialogueVerb *verb = &topic->verbs[j];
                if (!verb->entries) continue;
                for (uint32_t k = 0; k < verb->entry_count; k++) free(verb->entries[k].extra);
                free(verb->entries);
            }
            free(topic->verbs);
        }
        free(tree->topics);
    }
    free(tree->code);
    free(tree);
}

// ---- VM ----

void petka_dialoguevm_start(PetkaDialogueVM *vm, PetkaDialogueTree *tree, uint32_t start_offset) {
    vm->tree = tree;
    vm->ip = start_offset;
    vm->done = 0;
    vm->skip_end = 0;
}

static uint32_t op_of(uint32_t word) { return (word >> 24) & 0xFF; }

// Word offset just past the n-th DELIM at or after `from`.
static uint32_t skip_delims(const PetkaDialogueTree *tree, uint32_t from, uint32_t n) {
    uint32_t at = from;
    for (uint32_t seen = 0; seen < n && at < tree->code_count; at++)
        if (op_of(tree->code[at]) == PETKA_DLG_OP_DELIM) seen++;
    return at;
}

// A menu is laid out as
//   CHOICE(count | on/off bits << 8)
//   count label segments, each ending in DELIM; the label is the first
//       PHRASE of its segment (a few carry another word besides)
//   count GOTOs, one per option, into the code that plays it
// Each option's code speaks the label, plays the answer, usually switches
// itself off with DISABLE and jumps back to a menu or stops.
static uint32_t choice_count(uint32_t word) { return word & 0xFF; }
static int choice_enabled(uint32_t word, uint32_t option) { return (word >> (8 + option)) & 1; }

int petka_dialoguevm_choices(const PetkaDialogueVM *vm, uint32_t *out_phrases, int max) {
    const PetkaDialogueTree *tree = vm->tree;
    if (vm->done || vm->ip >= tree->code_count || op_of(tree->code[vm->ip]) != PETKA_DLG_OP_CHOICE) return 0;
    uint32_t word = tree->code[vm->ip];
    uint32_t at = vm->ip + 1;
    int n = 0;
    for (uint32_t option = 0; option < choice_count(word); option++) {
        uint32_t end = skip_delims(tree, at, 1);
        if (choice_enabled(word, option)) {
            uint32_t phrase = 0xFFFFFFFFu;
            for (uint32_t i = at; i < end; i++)
                if (op_of(tree->code[i]) == PETKA_DLG_OP_PHRASE) { phrase = tree->code[i] & 0xFFFFFF; break; }
            if (n < max) out_phrases[n] = phrase;
            n++;
        }
        at = end;
    }
    return n;
}

int petka_dialoguevm_choose(PetkaDialogueVM *vm, int n) {
    const PetkaDialogueTree *tree = vm->tree;
    if (vm->done || vm->ip >= tree->code_count || op_of(tree->code[vm->ip]) != PETKA_DLG_OP_CHOICE) return -1;
    uint32_t word = tree->code[vm->ip];
    uint32_t table = skip_delims(tree, vm->ip + 1, choice_count(word));
    for (uint32_t option = 0; option < choice_count(word); option++) {
        if (!choice_enabled(word, option)) continue;
        if (n-- > 0) continue;
        if (table + option >= tree->code_count) return -1;
        vm->ip = table + option; // a GOTO; the next step follows it
        return 0;
    }
    return -1;
}

// DISABLE/ENABLE: operand is (option << 16) | address of the CHOICE word.
static void set_option(PetkaDialogueTree *tree, uint32_t operand, int on) {
    uint32_t addr = operand & 0xFFFF, option = (operand >> 16) & 0xFF;
    if (addr >= tree->code_count || op_of(tree->code[addr]) != PETKA_DLG_OP_CHOICE || option >= 16) return;
    if (on) tree->code[addr] |= 1u << (8 + option);
    else tree->code[addr] &= ~(1u << (8 + option));
}

int petka_dialoguevm_next(PetkaDialogueVM *vm, uint32_t *out_phrase_index) {
    PetkaDialogueTree *tree = vm->tree;
    if (vm->done) return 0;

    while (vm->ip < tree->code_count) {
        uint32_t word = tree->code[vm->ip];
        uint32_t op = (word >> 24) & 0xFF;
        uint32_t operand = word & 0xFFFFFF;

        switch (op) {
            case PETKA_DLG_OP_STOP:
                vm->done = 1;
                return 0;
            case PETKA_DLG_OP_PHRASE:
                vm->ip++;
                *out_phrase_index = operand;
                return 1;
            case PETKA_DLG_OP_DELIM:
                // End of the SKIPN variant being played: carry on after the
                // whole group rather than falling into a sibling variant.
                // Almost every group is followed by STOP, a few by more
                // dialogue. (Inferred from the data, not traced in the exe.)
                if (vm->skip_end) {
                    vm->ip = vm->skip_end;
                    vm->skip_end = 0;
                    break;
                }
                vm->done = 1;
                return 0;
            case PETKA_DLG_OP_GOTO:
                vm->ip = operand & 0xFFFF;
                break;
            case PETKA_DLG_OP_SKIPN: {
                uint32_t n = operand;
                if (n == 0) { vm->ip++; break; }
                uint32_t pick = (uint32_t)rand() % n;
                vm->ip++;
                vm->skip_end = skip_delims(tree, vm->ip, n);
                for (uint32_t skipped = 0; skipped < pick && vm->ip < tree->code_count; vm->ip++) {
                    if (((tree->code[vm->ip] >> 24) & 0xFF) == PETKA_DLG_OP_DELIM) skipped++;
                }
                break;
            }
            case PETKA_DLG_OP_USERMSG:
                vm->ip++;
                *out_phrase_index = operand;
                return 2;
            case PETKA_DLG_OP_CHOICE: {
                // Player dialogue menu. Stay on it until the caller picks.
                uint32_t open = 0;
                for (uint32_t option = 0; option < choice_count(word); option++)
                    open += (uint32_t)choice_enabled(word, option);
                if (!open) { vm->done = 1; return 0; } // nothing left to say
                return 3;
            }
            case PETKA_DLG_OP_DISABLE:
                set_option(tree, operand, 0);
                vm->ip++;
                break;
            case PETKA_DLG_OP_ENABLE:
                set_option(tree, operand, 1);
                vm->ip++;
                break;
            default:
                // Not acted on here (e.g. the 0x04/0x05 menu-option
                // bookkeeping instructions) -- skip over it, best effort.
                vm->ip++;
                break;
        }
    }
    vm->done = 1;
    return 0;
}
