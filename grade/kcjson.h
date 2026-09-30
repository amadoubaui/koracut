/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Un JSON minimal, sans Qt : le module MLT (chargé aussi par melt) ne doit dépendre que de la
// bibliothèque standard. Lecture et écriture, nombres en double.

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kcgrade {

class Json
{
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool b);
    Json(double d);
    Json(int i);
    Json(const char *s);
    Json(const std::string &s);
    static Json array();
    static Json object();

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Null; }
    bool isNumber() const { return m_type == Number; }
    bool isString() const { return m_type == String; }
    bool isArray() const { return m_type == Array; }
    bool isObject() const { return m_type == Object; }
    bool isBool() const { return m_type == Bool; }

    double toNumber(double def = 0.) const;
    bool toBool(bool def = false) const;
    std::string toString(const std::string &def = std::string()) const;

    // Tableau
    size_t size() const;
    const Json &at(size_t i) const;
    void push(const Json &v);

    // Objet (ordre des clés conservé)
    bool has(const std::string &key) const;
    const Json &get(const std::string &key) const;
    double num(const std::string &key, double def) const;
    bool flag(const std::string &key, bool def) const;
    std::string str(const std::string &key, const std::string &def = std::string()) const;
    void set(const std::string &key, const Json &v);
    const std::vector<std::pair<std::string, Json>> &items() const { return m_obj; }

    std::string dump() const;
    static Json parse(const std::string &text, bool *ok = nullptr);

private:
    Type m_type = Null;
    bool m_bool = false;
    double m_num = 0.;
    std::string m_str;
    std::vector<Json> m_arr;
    std::vector<std::pair<std::string, Json>> m_obj;
    void dumpTo(std::string &out) const;
};

} // namespace kcgrade
