// A small JSON reader for the test fixtures (objects, arrays, strings, numbers, true, false,
// null). Throws std::runtime_error on malformed input. Test code only.
#pragma once

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace minijson
{
    struct Value
    {
        enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
        bool b = false;
        double num = 0.0;
        std::string str;
        std::vector<Value> arr;
        std::map<std::string, Value> obj;

        const Value& operator[] (const std::string& k) const
        {
            auto it = obj.find (k);
            if (type != Type::Object || it == obj.end())
                throw std::runtime_error ("missing key: " + k);
            return it->second;
        }
        const Value& operator[] (size_t i) const { return arr.at (i); }
        size_t size() const { return type == Type::Array ? arr.size() : obj.size(); }
        int asInt() const { return (int) num; }
    };

    class Parser
    {
    public:
        explicit Parser (const std::string& text) : s (text) {}

        Value parse()
        {
            Value v = value();
            ws();
            if (p != s.size())
                fail ("trailing characters");
            return v;
        }

    private:
        [[noreturn]] void fail (const char* what) const
        {
            throw std::runtime_error (std::string ("JSON: ") + what + " at offset " + std::to_string (p));
        }

        void ws() { while (p < s.size() && std::isspace ((unsigned char) s[p])) ++p; }

        char peek() { ws(); if (p >= s.size()) fail ("unexpected end"); return s[p]; }

        void expect (char c) { if (peek() != c) fail ("unexpected character"); ++p; }

        Value value()
        {
            const char c = peek();
            Value v;
            if (c == '{')
            {
                v.type = Value::Type::Object;
                ++p;
                if (peek() == '}') { ++p; return v; }
                for (;;)
                {
                    std::string k = string();
                    expect (':');
                    v.obj[k] = value();
                    if (peek() == ',') { ++p; continue; }
                    expect ('}');
                    return v;
                }
            }
            if (c == '[')
            {
                v.type = Value::Type::Array;
                ++p;
                if (peek() == ']') { ++p; return v; }
                for (;;)
                {
                    v.arr.push_back (value());
                    if (peek() == ',') { ++p; continue; }
                    expect (']');
                    return v;
                }
            }
            if (c == '"') { v.type = Value::Type::String; v.str = string(); return v; }
            if (s.compare (p, 4, "true") == 0) { p += 4; v.type = Value::Type::Bool; v.b = true; return v; }
            if (s.compare (p, 5, "false") == 0) { p += 5; v.type = Value::Type::Bool; return v; }
            if (s.compare (p, 4, "null") == 0) { p += 4; return v; }

            const char* start = s.c_str() + p;
            char* end = nullptr;
            v.num = std::strtod (start, &end);
            if (end == start)
                fail ("bad value");
            p += (size_t) (end - start);
            v.type = Value::Type::Number;
            return v;
        }

        std::string string()
        {
            expect ('"');
            std::string out;
            while (p < s.size() && s[p] != '"')
            {
                char c = s[p++];
                if (c == '\\')
                {
                    if (p >= s.size()) fail ("bad escape");
                    const char e = s[p++];
                    switch (e)
                    {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'u': out += '?'; p += 4; break;   // fixtures are ASCII
                        default: out += e; break;
                    }
                }
                else
                {
                    out += c;
                }
            }
            if (p >= s.size()) fail ("unterminated string");
            ++p;
            return out;
        }

        const std::string& s;
        size_t p = 0;
    };

    inline Value parseFile (const std::string& path)
    {
        std::ifstream f (path);
        if (! f)
            throw std::runtime_error ("can't open " + path);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        return Parser (text).parse();
    }
}
