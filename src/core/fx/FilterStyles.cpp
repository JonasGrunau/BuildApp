#include "FilterStyles.h"

#include "FilterStyleTables.h"

namespace ba::style
{

namespace
{
    constexpr int kN = 11;

    const data::Table* table (int style) noexcept { return isClean (style) ? nullptr : &data::kTables[style]; }

    // Bilinear: rows are the peak, columns the frequency, both clamped to the grid.
    double lookup (const float (&t)[kN][kN], int row, double rowFrac, int col, double colFrac) noexcept
    {
        const int c1 = std::min (col + 1, kN - 1), r1 = std::min (row + 1, kN - 1);
        const double a = t[row][col] + colFrac * (t[row][c1] - t[row][col]);
        const double b = t[r1][col] + colFrac * (t[r1][c1] - t[r1][col]);
        return a + rowFrac * (b - a);
    }
} // namespace

const char* name (int style) noexcept
{
    if (const auto* t = table (style)) return t->name;
    return "Clean";
}

double inputGain (int style) noexcept
{
    const auto* t = table (style);
    return t != nullptr ? t->gain : 1.0;
}

double hardness (int style) noexcept
{
    const auto* t = table (style);
    return t != nullptr ? t->hardness : 1.0;
}

Voice voice (int style, double log2Hz, double peak) noexcept
{
    const auto* t = table (style);
    if (t == nullptr) return { std::min (peak, 0.999), 0.0, 0.1 };

    const double col = std::clamp ((log2Hz - t->lowLog2) * (kN - 1) / (t->highLog2 - t->lowLog2), 0.0, kN - 1.0);
    const double row = std::clamp ((kN - 1) * peak, 0.0, kN - 1.0);
    const int c = (int) col, r = (int) row;
    const double cf = col - c, rf = row - r;
    return { lookup (t->peak, r, rf, c, cf), lookup (t->cubic, r, rf, c, cf), std::max (0.1, lookup (t->clip, r, rf, c, cf)) };
}

} // namespace ba::style
