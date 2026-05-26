// rando_textfield.h — SDL_TEXTINPUT widget (tasks.md §9.1). Stub.
//
// Single-line text buffer with cursor, backspace, paste-from-clipboard,
// base32-constrained input. Drives the share-string entry field on PC and
// Switch builds.

#ifndef ZELDA3_RANDO_TEXTFIELD_H_
#define ZELDA3_RANDO_TEXTFIELD_H_

#include "../types.h"

#define kRandoTextFieldMaxLen 64

typedef struct RandoTextField {
  char buf[kRandoTextFieldMaxLen + 1];  // +1 for NUL
  int len;
  int cursor;
  bool active;
} RandoTextField;

// Initialize a text field. base32_only restricts input to RFC 4648 Base32
// chars (used for share-string entry).
void TextField_Init(RandoTextField *tf, bool base32_only);

// Handle a single character of input. No-op if !active or if base32_only
// and the char is outside the alphabet.
void TextField_HandleChar(RandoTextField *tf, char c);

// Handle a special key (backspace, paste, etc.). Returns true if the key
// was consumed.
typedef enum {
  kTextFieldKey_Backspace,
  kTextFieldKey_Delete,
  kTextFieldKey_Paste,
  kTextFieldKey_Submit,
} TextFieldKey;
bool TextField_HandleKey(RandoTextField *tf, TextFieldKey key);

#endif  // ZELDA3_RANDO_TEXTFIELD_H_
