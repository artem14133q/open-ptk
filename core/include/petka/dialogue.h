// Reader + tiny VM for Petka's dialogue/interaction bytecode.
// Format reverse-engineered from Bigdlg.cpp (BigDialogue) -- see
// docs/str-format.md for the full derivation and byte-exact verification.
//
// PART_N/DIALOGUE.LOD  -- phrase table (voice clip filenames) + CP1251 text
// PART_N/DIALOGUE.FIX  -- topic -> verb -> bytecode-entry-point tree, plus
//                         one shared bytecode array every entry point
//                         indexes into.
#ifndef PETKA_DIALOGUE_H
#define PETKA_DIALOGUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- DIALOGUE.LOD: phrase table + text ----

typedef struct {
    char wav_name[17]; // NUL-terminated, e.g. "11_01701.wav"
    char *text;        // owned, CP1251 bytes (not converted), NUL-terminated
} PetkaPhrase;

typedef struct {
    PetkaPhrase *phrases;
    uint32_t count;
} PetkaPhraseTable;

// Parses a whole DIALOGUE.LOD buffer. Returns NULL on failure.
PetkaPhraseTable *petka_phrasetable_open(const uint8_t *data, size_t len);
void petka_phrasetable_close(PetkaPhraseTable *table);

// ---- DIALOGUE.FIX: topic/verb tree + shared bytecode ----

typedef struct {
    uint32_t bytecode_offset; // SubsubRecord.field0: word offset into `code`
    uint32_t extra_count;
    uint32_t *extra; // owned, extra_count words (usually empty)
} PetkaDialogueEntry;

typedef struct {
    uint32_t verb_id;
    uint32_t data;
    uint32_t entry_count;
    PetkaDialogueEntry *entries;
} PetkaDialogueVerb;

typedef struct {
    uint32_t topic_id;
    uint32_t verb_count;
    PetkaDialogueVerb *verbs;
} PetkaDialogueTopic;

typedef struct {
    PetkaDialogueTopic *topics;
    uint32_t topic_count;

    uint32_t *code; // owned, shared bytecode words
    uint32_t code_count;
} PetkaDialogueTree;

// Parses a whole DIALOGUE.FIX buffer. Returns NULL on failure (including
// "doesn't consume the file exactly" -- the format has no other integrity
// check, so exact EOF alignment is treated as the validity check).
PetkaDialogueTree *petka_dialoguetree_open(const uint8_t *data, size_t len);
void petka_dialoguetree_close(PetkaDialogueTree *tree);

// ---- bytecode VM ----

typedef enum {
    PETKA_DLG_OP_DELIM = 1,
    PETKA_DLG_OP_CHOICE = 2,
    PETKA_DLG_OP_GOTO = 3,
    PETKA_DLG_OP_STOP = 6,
    PETKA_DLG_OP_PHRASE = 7,
    PETKA_DLG_OP_SKIPN = 8,
    PETKA_DLG_OP_USERMSG = 9,
} PetkaDialogueOp;

typedef struct {
    const PetkaDialogueTree *tree;
    uint32_t ip; // current word offset into tree->code
    int done;
} PetkaDialogueVM;

void petka_dialoguevm_start(PetkaDialogueVM *vm, const PetkaDialogueTree *tree, uint32_t start_offset);

// Runs the VM forward until it hits a PHRASE (returns its phrase index via
// *out_phrase_index and leaves the VM positioned just after it, ready for
// the next call) or STOP (returns 0 with *out_phrase_index unset). SKIPN
// picks a pseudo-random alternative each time it's encountered. Returns 1
// if a phrase was produced, 0 on STOP/end, -1 on malformed bytecode.
//
// Returns 2 on USERMSG, with its argument in *out_phrase_index: the
// dialogue pauses here and the caller posts a USERMSG script message with
// that argument to Chapayev (see petka/script.h); the script answers with
// CONTINUE, and the next call carries on from after the USERMSG.
int petka_dialoguevm_next(PetkaDialogueVM *vm, uint32_t *out_phrase_index);

#ifdef __cplusplus
}
#endif

#endif
