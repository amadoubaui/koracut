/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kcjson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>

namespace kcgrade {

namespace {
const Json &nullJson()
{
    static const Json n;
    return n;
}

struct Parser
{
    const std::string &s;
    size_t i = 0;
    bool ok = true;

    void ws()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) {
            ++i;
        }
    }
    bool eat(char c)
    {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    std::string string()
    {
        std::string out;
        ++i; // guillemet ouvrant
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) {
                        ok = false;
                        return out;
                    }
                    unsigned cp = unsigned(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        unsigned lo = unsigned(std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: out += e;
                }
            } else {
                out += c;
            }
        }
        if (i >= s.size()) {
            ok = false;
        } else {
            ++i;
        }
        return out;
    }
    Json value()
    {
        ws();
        if (i >= s.size()) {
            ok = false;
            return Json();
        }
        char c = s[i];
        if (c == '{') {
            ++i;
            Json o = Json::object();
            if (eat('}')) {
                return o;
            }
            while (ok) {
                ws();
                if (i >= s.size() || s[i] != '"') {
                    ok = false;
                    break;
                }
                std::string k = string();
                if (!eat(':')) {
                    ok = false;
                    break;
                }
                o.set(k, value());
                if (eat(',')) {
                    continue;
                }
                if (!eat('}')) {
                    ok = false;
                }
                break;
            }
            return o;
        }
        if (c == '[') {
            ++i;
            Json a = Json::array();
            if (eat(']')) {
                return a;
            }
            while (ok) {
                a.push(value());
                if (eat(',')) {
                    continue;
                }
                if (!eat(']')) {
                    ok = false;
                }
                break;
            }
            return a;
        }
        if (c == '"') {
            return Json(string());
        }
        if (s.compare(i, 4, "true") == 0) {
            i += 4;
            return Json(true);
        }
        if (s.compare(i, 5, "false") == 0) {
            i += 5;
            return Json(false);
        }
        if (s.compare(i, 4, "null") == 0) {
            i += 4;
            return Json();
        }
        // Nombre : lecture indépendante de la locale (MLT peut tourner sous une locale à virgule).
        size_t start = i;
        while (i < s.size() && (std::strchr("+-0123456789.eE", s[i]) != nullptr)) {
            ++i;
        }
        if (start == i) {
            ok = false;
            return Json();
        }
        std::istringstream in(s.substr(start, i - start));
        in.imbue(std::locale::classic());
        double d = 0.;
        in >> d;
        return Json(d);
    }
};

void dumpString(std::string &out, const std::string &str)
{
    out += '"';
    for (unsigned char c : str) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    out += '"';
}
} // namespace

Json::Json(bool b)
    : m_type(Bool)
    , m_bool(b)
{
}
Json::Json(double d)
    : m_type(Number)
    , m_num(d)
{
}
Json::Json(int i)
    : m_type(Number)
    , m_num(i)
{
}
Json::Json(const char *s)
    : m_type(String)
    , m_str(s)
{
}
Json::Json(const std::string &s)
    : m_type(String)
    , m_str(s)
{
}
Json Json::array()
{
    Json j;
    j.m_type = Array;
    return j;
}
Json Json::object()
{
    Json j;
    j.m_type = Object;
    return j;
}

double Json::toNumber(double def) const
{
    if (m_type == Number) {
        return m_num;
    }
    if (m_type == Bool) {
        return m_bool ? 1. : 0.;
    }
    return def;
}
bool Json::toBool(bool def) const
{
    if (m_type == Bool) {
        return m_bool;
    }
    if (m_type == Number) {
        return m_num != 0.;
    }
    return def;
}
std::string Json::toString(const std::string &def) const
{
    return m_type == String ? m_str : def;
}
size_t Json::size() const
{
    return m_type == Array ? m_arr.size() : (m_type == Object ? m_obj.size() : 0);
}
const Json &Json::at(size_t i) const
{
    return (m_type == Array && i < m_arr.size()) ? m_arr[i] : nullJson();
}
void Json::push(const Json &v)
{
    if (m_type != Array) {
        *this = array();
    }
    m_arr.push_back(v);
}
bool Json::has(const std::string &key) const
{
    for (const auto &kv : m_obj) {
        if (kv.first == key) {
            return true;
        }
    }
    return false;
}
const Json &Json::get(const std::string &key) const
{
    for (const auto &kv : m_obj) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return nullJson();
}
double Json::num(const std::string &key, double def) const
{
    return get(key).toNumber(def);
}
bool Json::flag(const std::string &key, bool def) const
{
    return get(key).toBool(def);
}
std::string Json::str(const std::string &key, const std::string &def) const
{
    return get(key).toString(def);
}
void Json::set(const std::string &key, const Json &v)
{
    if (m_type != Object) {
        *this = object();
    }
    for (auto &kv : m_obj) {
        if (kv.first == key) {
            kv.second = v;
            return;
        }
    }
    m_obj.emplace_back(key, v);
}

void Json::dumpTo(std::string &out) const
{
    switch (m_type) {
    case Null: out += "null"; break;
    case Bool: out += m_bool ? "true" : "false"; break;
    case Number: {
        if (!std::isfinite(m_num)) {
            out += "0";
            break;
        }
        // Six chiffres significatifs suffisent (les réglages ont trois décimales), sans locale.
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.precision(7);
        o << m_num;
        out += o.str();
        break;
    }
    case String: dumpString(out, m_str); break;
    case Array:
        out += '[';
        for (size_t i = 0; i < m_arr.size(); ++i) {
            if (i) {
                out += ',';
            }
            m_arr[i].dumpTo(out);
        }
        out += ']';
        break;
    case Object:
        out += '{';
        for (size_t i = 0; i < m_obj.size(); ++i) {
            if (i) {
                out += ',';
            }
            dumpString(out, m_obj[i].first);
            out += ':';
            m_obj[i].second.dumpTo(out);
        }
        out += '}';
        break;
    }
}

std::string Json::dump() const
{
    std::string out;
    dumpTo(out);
    return out;
}

Json Json::parse(const std::string &text, bool *ok)
{
    Parser p{text};
    Json v = p.value();
    p.ws();
    if (p.i != text.size()) {
        p.ok = false;
    }
    if (ok) {
        *ok = p.ok;
    }
    return p.ok ? v : Json();
}

} // namespace kcgrade
