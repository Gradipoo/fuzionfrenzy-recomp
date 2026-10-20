// The console cheats, entered the game's own way (common/cheats.h, offered in the F1 panel).
//
// None has been identified for Fuzion Frenzy yet, so this file has nothing to enter and touches no
// guest memory. The sibling port this one is built from offers its codes because, in its own image
// and by address, it found:
//   - the recogniser: per-frame code that keeps the last few single-button presses and matches them
//     against the game's table of pad sequences;
//   - that table: each code's sequence (built in code at startup there, so no byte search of the
//     image found it);
//   - the setter a matched sequence calls with the row - the game's own toggle of that code - and
//     the mask word it flips, which is what the panel's checkboxes read;
//   - the game's own conditions for taking a code (a cheat latch, the objects the recogniser needs),
//     checked again on the title thread before each entry.
// The host there enters a code exactly as a matched sequence does: an override of the per-frame
// update calls the setter on the title thread, under those same conditions, for every code the
// panel asked for.
//
// In this image none of that has been looked for: no cheat code, no table, no setter address and no
// per-frame function to enter one from has been identified, nor whether the game has console cheat
// codes at all. The table below is empty for exactly that reason. A row goes in only with its pad
// sequence read out of this image and the setter it reaches found by address and confirmed in the
// disassembly, never by analogy with another title; InGame, On and Toggle then read and drive that
// state the way the sibling's do.
#include <array>

#include "common/cheats.h"

namespace {

// Empty: no cheat code and no cheat setter address has been identified for Fuzion Frenzy yet.
constexpr std::array<cheats::Cheat, 0> kTable{};

}  // namespace

namespace cheats {

std::span<const Cheat> Table() { return kTable; }

// No state of the game has been found that says it would take a code, and there is no setter to
// take one through: never.
bool InGame() { return false; }

// No cheat mask of the game's has been found to read, and nothing can have been entered.
bool On(Code) { return false; }

// No setter to enter a code through, and so nothing to queue for the title thread.
void Toggle(Code) {}

}  // namespace cheats
