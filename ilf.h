/*

    Copyright (c) 2023 The MITRE Corporation.

    ALL RIGHTS RESERVED. This copyright notice must

    not be removed from this software, absent MITRE's

    express written permission.

*/

#pragma once

#include <string>
#include <vector>
#include <iostream>

namespace libilf {

struct KeyValue {
    KeyValue() {
        _key = "";
        _value = "";
        _has_quotes = true;
    }

    KeyValue(std::string const& key, std::string const& value, bool has_quotes) : 
        _key(key), 
        _value(value),
        _has_quotes(has_quotes) { }

    std::string _key, _value;
    bool _has_quotes;
};

struct ILF {
    ILF() {
        _event_t = "";
        _sender = "";
        _receiver = "";
        _time = "";
        _pairs = std::vector<KeyValue>();
    }

    ILF(std::string const& event_t, 
        std::string const& sender, 
        std::string const& receiver, 
        std::string const& time) : 
        _event_t(event_t),
        _sender(sender),
        _receiver(receiver),
        _time(time) { }
    
    std::string _event_t,
            _sender,
            _receiver,
            _time;
    std::vector<KeyValue> _pairs;
};

inline std::ostream& operator<<(std::ostream& os, KeyValue const& key_value) {
    if (key_value._has_quotes) {
        os << key_value._key << "=\"" << key_value._value << "\"";
    } else {
        os << key_value._key << "=" << key_value._value << "";
    }
    return os;
}

inline std::string& operator<<(std::string& str, KeyValue const& key_value) {
    if (key_value._has_quotes) {
        str += key_value._key + "=\"" + key_value._value + "\"";
    } else {
        str += key_value._key + "=" + key_value._value;
    }
    return str;
}

inline bool operator==(KeyValue const& kv1, KeyValue const& kv2) {
    return (kv1._key == kv2._key && kv1._value == kv2._value);
}

inline std::ostream& operator<<(std::ostream& os, ILF const& ilf) {
    os << ilf._event_t << "[" << ilf._sender << "," <<
          ilf._receiver << "," << ilf._time << ",(";
    if (ilf._pairs.empty()) {
        os << ")]";
        return os;
    }
    unsigned long i;
    for (i = 0; i < ilf._pairs.size() - 1; i++) {
        os << ilf._pairs[i] << ";";
    }
    os << ilf._pairs[i] << ")]";
    return os;
}

inline std::string& operator<<(std::string& str, ILF const& ilf) {
    str += ilf._event_t + "[" + ilf._sender + "," + 
        ilf._receiver + "," + ilf._time + ",(";
        if (ilf._pairs.empty()) {
            str += ")] ";
            return str;
        }
        unsigned long i;
        for (i = 0; i < ilf._pairs.size() - 1; i++) {
            str << ilf._pairs[i]; // calls the overloaded operator
            str += ";";
        }
        str << ilf._pairs[i];
        str += ")] ";
        return str;
}

inline bool operator==(ILF const& ilf1, ILF const& ilf2) {
    bool equal = (
        ilf1._event_t == ilf2._event_t && 
        ilf1._sender == ilf2._sender && 
        ilf1._receiver == ilf2._receiver && 
        ilf1._time == ilf2._time
    );
    if (!equal) {
        return equal;
    }
    if (ilf1._pairs.size() != ilf2._pairs.size()) {
        return false;
    }
    auto it1 = ilf1._pairs.begin(), it2 = ilf2._pairs.begin();
    while (it1 != ilf1._pairs.end() && equal) {
        equal = (*it1 == *it2);
        it1++;
        it2++;
    }
    return equal;
}

}
