#pragma once

#include "Presets.h"

#include <string>
#include <string_view>

// A preset's path as text: what the project state and a user preset file
// keep (cut types, steps and every key, lossless: floats in nine digits,
// whatever the host's locale). Version 1, one line each:
//   buildapp-path 1
//   lowCutType 3
//   highCutType -1
//   lane lowCut 0 3  0 0 1 0  0.15 0.22 1 0  0.5 0.4 1 0
//        (lane key, step, count, then at value curve offset per key)
// Lines it doesn't know are skipped (a newer version may add some). What it
// reads is clamped, sorted and capped (LaneEdit.h), never thrown at; a lane
// keeps a cut only with that cut's type.
namespace ba
{
    std::string toText (const Preset&);
    // Reads a path into `out`'s types and lanes (its name, section and help
    // stay). False when the text isn't one: `out` then drives nothing.
    bool fromText (std::string_view, Preset& out);
} // namespace ba
