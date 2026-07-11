#ifndef ZELDA3_RANDO_DIALOGUE_H_
#define ZELDA3_RANDO_DIALOGUE_H_

#include "../types.h"

// Rewrite reward-specific vanilla dialogue after Text_LoadCharacterBuffer has
// expanded the ROM text into font codes. No-op outside an active rando slot or
// for any non-US language; the generated reward text currently uses the US
// command grammar and font alphabet.
void Rando_RewriteRewardDialogue(uint16 msg_id, uint8 *buffer);

// Focused renderer/mapping regression checks, invoked by --rando-selftest.
void RandoDialogue_SelfCheck(void);

#endif  // ZELDA3_RANDO_DIALOGUE_H_
