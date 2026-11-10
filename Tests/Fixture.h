// The lattice reference fixture (Tests/fixtures/lattice_reference.json, written by
// Tools/gen_lattice_fixture.py) and the chord types it lists. Test code only.
#pragma once

#include "MiniJson.h"
#include "core/lattice/Ops.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace fixture
{
    inline const minijson::Value& lattice()
    {
        static const minijson::Value v = []
        {
            const char* dir = std::getenv ("TONNETZML_FIXTURES");
            return minijson::parseFile (std::string (dir != nullptr ? dir : "Tests/fixtures") + "/lattice_reference.json");
        }();
        return v;
    }

    struct ChordType
    {
        std::string name;
        std::vector<int> intervals;

        unsigned maskAt (int root) const
        {
            unsigned m = 0;
            for (int i : intervals)
                m |= 1u << tonnetz::mod12 (root + i);
            return m;
        }

        tonnetz::Chord at (int root, int parity = 0) const
        {
            auto c = tonnetz::makeChord (maskAt (root), root);
            c.parity = (uint8_t) parity;
            return c;
        }
    };

    inline const std::vector<ChordType>& chordTypes()
    {
        static const std::vector<ChordType> types = []
        {
            std::vector<ChordType> t;
            const auto& list = lattice()["chordTypes"];
            for (size_t i = 0; i < list.size(); ++i)
            {
                ChordType c;
                c.name = list[i]["name"].str;
                for (size_t j = 0; j < list[i]["intervals"].size(); ++j)
                    c.intervals.push_back (list[i]["intervals"][j].asInt());
                t.push_back (c);
            }
            return t;
        }();
        return types;
    }

    // [mask, anchor, parity, available] as the fixture writes a result.
    inline tonnetz::Chord chordOf (const minijson::Value& r)
    {
        tonnetz::Chord c;
        c.mask = (tonnetz::PcMask) r[0].asInt();
        c.anchor = (int8_t) r[1].asInt();
        c.parity = (uint8_t) r[2].asInt();
        return c;
    }

    inline std::string describe (const tonnetz::Chord& c)
    {
        std::string s = "{";
        for (int pc = 0; pc < 12; ++pc)
            if (c.has (pc))
                s += (s.size() > 1 ? " " : "") + std::to_string (pc);
        return s + "} anchor " + std::to_string (c.anchor) + " parity " + std::to_string (c.parity);
    }
}
