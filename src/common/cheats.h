#pragma once

#include <span>

// The game's own console cheats, offered in the F1 panel (overlay.cpp). Each one is entered the way
// its pad code enters it (src/game/cheats.cpp), so what it does is the game's own: nothing here is a
// setting, nothing is saved, and nothing happens until the player clicks.
namespace cheats {

// Rows of the game's cheat-code table, by their index there: entering row n is the game's own
// toggle of that code. None is listed because none has been identified in Fuzion Frenzy yet
// (src/game/cheats.cpp says what is missing).
enum Code : int {};

// one code as the F1 panel offers it
struct Cheat {
    Code code;          // its row in the game's table
    const char* label;  // the panel's name for it
    const char* note;   // its pad sequence and what the game does with it
};

// any thread: the codes identified in this title, in the game's table order. Empty for now, and the
// F1 panel says so instead of offering any.
std::span<const Cheat> Table();

// any thread: the game is initialised and in a state where a code can be accepted on the next frame
bool InGame();

// any thread: the code is on - the game's own state of it, with a click the title thread has not
// taken yet already counted, so a checkbox shows what was asked for
bool On(Code c);
// render thread: enter the code again (on <-> off) on the title thread's next frame, exactly as the
// pad sequence would; nothing is queued outside the game
void Toggle(Code c);

}  // namespace cheats
