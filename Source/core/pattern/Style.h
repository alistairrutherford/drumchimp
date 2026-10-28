// A drum style as text (PLAN.md §3.4): the data the pattern generator works from. Plain C++,
// no JUCE. The factory styles are in FactoryStyles.h; adding one is writing a block like this:
//
//   name     = House
//   tempo    = 124              the style's usual BPM (shown; the host sets the tempo)
//   meter    = 4/4              4/4 (the default), 3/4 or 6/8
//   grid     = 16               steps per bar: 4/4 has 16 (sixteenths) or 12 (triplet eighths);
//                               3/4 and 6/8 have 12 sixteenths, felt in 3 beats or in 2 (dotted)
//   swing    = 54               % (50 straight … 75): the default for the Swing knob
//   families = 909 808 707      kit families Randomise leans to for this style
//   fill     = snare            the fill: none, toms or snare
//   crash    = one              a crash on the pattern's first beat (and after fills); or fills
//   extras   = closedHat perc   lanes that may gain hits on empty steps at high Complexity
//   kick     = X...X...X...X...
//   clap     = ....X.......X...
//
// Every other key is a lane: a pad role (kick, snare, clap, rim, closedHat, openHat, lowTom,
// midTom, highTom, tom for any tom, crash, ride, perc) and one or more bars of steps (spaces
// and | are ignored, for reading):
//
//   X  anchor: always plays, accented       o  variation: more likely with Complexity
//   x  core: plays (thins out at low Density)  g  ghost note: quiet, with Ghost notes on
//   r  core, and may roll (2 or 3 hits in the step) with Complexity      .  rest
#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace drumchimp
{
    struct StyleLane
    {
        std::string role;                 // a PadRole id, or "tom"
        std::string steps;                // one or more bars of X x o g r .
    };

    struct Style
    {
        enum class Fill { none, toms, snare };

        std::string name;
        int grid = 16;
        int numerator = 4, denominator = 4;   // the meter

        // Quarter-note beats in a bar (4/4: 4, 3/4 and 6/8: 3), and steps in a felt beat (a
        // quarter, a triplet quarter, or 6/8's dotted quarter): accents, fills and grid lines.
        double beatsPerBar() const noexcept { return numerator * 4.0 / denominator; }
        int stepsPerBeat() const noexcept
        {
            if (numerator == 6 && denominator == 8) return grid / 2;
            return (int) (grid / beatsPerBar());
        }
        double tempo = 120.0;
        double swing = 0.5;               // 0.5 straight … 0.75
        std::vector<std::string> families, extras;
        Fill fill = Fill::snare;
        bool crashOnOne = false;
        std::vector<StyleLane> lanes;
    };

    namespace style
    {
        inline std::string trim (const std::string& s)
        {
            const auto a = s.find_first_not_of (" \t\r");
            const auto b = s.find_last_not_of (" \t\r");
            return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
        }

        inline std::vector<std::string> words (const std::string& s)
        {
            std::vector<std::string> out;
            std::istringstream in (s);
            for (std::string w; in >> w;)
                out.push_back (w);
            return out;
        }

        inline bool isLaneRole (const std::string& r)
        {
            static const char* roles[] { "kick", "snare", "clap", "rim", "closedHat", "openHat", "lowTom", "midTom", "highTom", "tom", "crash", "ride", "perc" };
            for (const char* known : roles)
                if (r == known)
                    return true;
            return false;
        }
    }

    // Reads a style. Returns false, with `error`, if the text isn't one.
    inline bool parseStyle (const std::string& text, Style& out, std::string& error)
    {
        using namespace style;
        out = {};
        std::istringstream in (text);
        int lineNumber = 0;
        for (std::string line; std::getline (in, line);)
        {
            ++lineNumber;
            if (const auto hash = line.find ('#'); hash != std::string::npos)
                line.erase (hash);
            line = trim (line);
            if (line.empty())
                continue;
            const auto eq = line.find ('=');
            if (eq == std::string::npos)
            {
                error = "line " + std::to_string (lineNumber) + ": expected key = value";
                return false;
            }
            const auto key = trim (line.substr (0, eq)), value = trim (line.substr (eq + 1));
            try
            {
                if (key == "name") out.name = value;
                else if (key == "grid") out.grid = std::stoi (value);
                else if (key == "meter")
                {
                    if (value == "4/4") { out.numerator = 4; out.denominator = 4; }
                    else if (value == "3/4") { out.numerator = 3; out.denominator = 4; }
                    else if (value == "6/8") { out.numerator = 6; out.denominator = 8; }
                    else { error = "line " + std::to_string (lineNumber) + ": meter is 4/4, 3/4 or 6/8"; return false; }
                }
                else if (key == "tempo") out.tempo = std::stod (value);
                else if (key == "swing") out.swing = std::stod (value) / 100.0;
                else if (key == "families") out.families = words (value);
                else if (key == "extras") out.extras = words (value);
                else if (key == "crash") out.crashOnOne = value == "one";
                else if (key == "fill")
                {
                    if (value == "none") out.fill = Style::Fill::none;
                    else if (value == "toms") out.fill = Style::Fill::toms;
                    else if (value == "snare") out.fill = Style::Fill::snare;
                    else { error = "line " + std::to_string (lineNumber) + ": fill is none, toms or snare"; return false; }
                }
                else if (isLaneRole (key))
                {
                    std::string steps;
                    for (const char c : value)
                    {
                        if (c == ' ' || c == '|')
                            continue;
                        if (std::string ("Xxogr.").find (c) == std::string::npos)
                        {
                            error = "line " + std::to_string (lineNumber) + ": '" + std::string (1, c) + "' isn't a step (X x o g r .)";
                            return false;
                        }
                        steps += c;
                    }
                    out.lanes.push_back ({ key, steps });
                }
                else
                {
                    error = "line " + std::to_string (lineNumber) + ": unknown key '" + key + "'";
                    return false;
                }
            }
            catch (...)
            {
                error = "line " + std::to_string (lineNumber) + ": '" + value + "' isn't a number";
                return false;
            }
        }

        if (out.name.empty()) { error = "the style has no name"; return false; }
        if (out.grid != 12 && out.grid != 16) { error = "grid is 12 or 16"; return false; }
        if (out.numerator != 4 && out.grid != 12) { error = "3/4 and 6/8 have a grid of 12 (sixteenths)"; return false; }
        if (out.swing < 0.5 || out.swing > 0.75) { error = "swing is 50 to 75"; return false; }
        if (out.lanes.empty()) { error = "the style has no lanes"; return false; }
        for (const auto& lane : out.lanes)
            if (lane.steps.empty() || lane.steps.size() % (size_t) out.grid != 0)
            {
                error = lane.role + ": " + std::to_string (lane.steps.size()) + " steps isn't whole bars of " + std::to_string (out.grid);
                return false;
            }
        for (const auto& e : out.extras)
            if (! isLaneRole (e)) { error = "extras: unknown lane '" + e + "'"; return false; }
        return true;
    }
}
