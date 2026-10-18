#include "PresetText.h"

#include "LaneEdit.h"

#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace ba
{
std::string toText (const Preset& p)
{
    std::ostringstream os;
    os.imbue (std::locale::classic());
    os << std::setprecision (9);
    os << "buildapp-path 1\n";
    os << "lowCutType " << p.lowCutType << "\n";
    os << "highCutType " << p.highCutType << "\n";
    for (const auto id : kLaneIds)
    {
        const Lane& l = lane (p, id);
        if (! l.driven()) continue;
        os << "lane " << laneKey (id) << ' ' << l.step << ' ' << l.count;
        for (const auto& k : l.points()) os << "  " << k.at << ' ' << k.value << ' ' << k.curve << ' ' << k.offset;
        os << "\n";
    }
    return os.str();
}

bool fromText (std::string_view text, Preset& out)
{
    out.lowCutType = out.highCutType = -1;
    for (const auto id : kLaneIds) lane (out, id) = Lane {};

    std::istringstream is { std::string (text) };
    is.imbue (std::locale::classic());
    bool isPath = false;
    std::string line;
    while (std::getline (is, line))
    {
        std::istringstream ls (line);
        ls.imbue (std::locale::classic());
        std::string word;
        if (! (ls >> word)) continue;
        if (word == "buildapp-path")
        {
            int version = 0;
            isPath = (bool) (ls >> version) && version >= 1;
            continue;
        }
        if (! isPath) continue;

        const auto type = [&ls] (int& t, size_t n) {
            int v = -1;
            if (ls >> v) t = v >= 0 && v < (int) n ? v : -1;
        };
        if (word == "lowCutType") type (out.lowCutType, std::size (kLowCutCharacters));
        else if (word == "highCutType") type (out.highCutType, std::size (kHighCutCharacters));
        else if (word == "lane")
        {
            std::string name;
            float step = 0.f;
            int count = 0;
            if (! (ls >> name >> step >> count)) continue;
            for (const auto id : kLaneIds)
            {
                if (name != laneKey (id)) continue;
                std::vector<Key> keys;
                for (int i = 0; i < count && i < 4096; ++i)
                {
                    Key k;
                    if (! (ls >> k.at >> k.value >> k.curve >> k.offset)) break;
                    keys.push_back (k);
                }
                Lane& l = lane (out, id);
                edit::assign (l, keys, laneRange (id));
                l.step = std::isfinite (step) && step > 0.f ? step : 0.f;
            }
        }
    }
    // A cut's lanes play only with its type.
    if (out.lowCutType < 0) out.lowCut = out.lowReso = Lane {};
    if (out.highCutType < 0) out.highCut = out.highReso = Lane {};
    if (! isPath)
        for (const auto id : kLaneIds) lane (out, id) = Lane {};
    return isPath;
}
} // namespace ba
