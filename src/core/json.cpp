#include "core/json.hpp"

#include <cctype>
#include <cstdlib>

namespace sim::json {

const Value* Value::find(const std::string& key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& [k, v] : object)
        if (k == key) return &v;
    return nullptr;
}

const char* Value::kind_name() const {
    switch (kind) {
        case Kind::Null: return "null";
        case Kind::Bool: return "boolean";
        case Kind::Number: return "number";
        case Kind::String: return "string";
        case Kind::Array: return "array";
        case Kind::Object: return "object";
    }
    return "?";
}

namespace {

class Parser {
public:
    Parser(const std::string& text, const std::string& source)
        : text_(text), source_(source) {}

    Value parse_document() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        if (pos_ != text_.size()) fail("trailing characters after the document");
        return v;
    }

private:
    const std::string& text_;
    const std::string& source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    [[noreturn]] void fail(const std::string& what) const {
        throw ParseError(source_ + ":" + std::to_string(line_) + ":" +
                         std::to_string(col_) + ": " + what);
    }

    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return at_end() ? '\0' : text_[pos_]; }

    char take() {
        if (at_end()) fail("unexpected end of input");
        char c = text_[pos_++];
        if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
        return c;
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        take();
    }

    void skip_ws() {
        while (!at_end()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') take();
            else break;
        }
    }

    Value parse_value() {
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') { Value v; v.kind = Value::Kind::String; v.str = parse_string(); return v; }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        if (text_.compare(pos_, 4, "true") == 0) { advance(4); Value v; v.kind = Value::Kind::Bool; v.boolean = true; return v; }
        if (text_.compare(pos_, 5, "false") == 0) { advance(5); Value v; v.kind = Value::Kind::Bool; v.boolean = false; return v; }
        if (text_.compare(pos_, 4, "null") == 0) { advance(4); return Value{}; }
        if (at_end()) fail("unexpected end of input");
        fail(std::string("unexpected character '") + c + "'");
    }

    void advance(std::size_t n) { for (std::size_t i = 0; i < n; ++i) take(); }

    Value parse_object() {
        Value v;
        v.kind = Value::Kind::Object;
        expect('{');
        skip_ws();
        if (peek() == '}') { take(); return v; }
        for (;;) {
            skip_ws();
            if (peek() != '"') fail("expected a string key");
            std::string key = parse_string();
            for (const auto& [k, _] : v.object)
                if (k == key) fail("duplicate key \"" + key + "\"");
            skip_ws();
            expect(':');
            skip_ws();
            Value item = parse_value();
            v.object.emplace_back(std::move(key), std::move(item));
            skip_ws();
            char c = take();
            if (c == '}') return v;
            if (c != ',') fail("expected ',' or '}' in object");
        }
    }

    Value parse_array() {
        Value v;
        v.kind = Value::Kind::Array;
        expect('[');
        skip_ws();
        if (peek() == ']') { take(); return v; }
        for (;;) {
            skip_ws();
            v.array.push_back(parse_value());
            skip_ws();
            char c = take();
            if (c == ']') return v;
            if (c != ',') fail("expected ',' or ']' in array");
        }
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    unsigned parse_hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = take();
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        for (;;) {
            char c = take();
            if (c == '"') return out;
            if (c == '\\') {
                char e = take();
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // surrogate pair
                            if (take() != '\\' || take() != 'u') fail("unpaired surrogate");
                            unsigned lo = parse_hex4();
                            if (lo < 0xDC00 || lo > 0xDFFF) fail("bad low surrogate");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: fail("bad escape sequence");
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character inside string");
            } else {
                out += c;
            }
        }
    }

    Value parse_number() {
        std::size_t start = pos_;
        if (peek() == '-') take();
        if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("bad number");
        if (peek() == '0') take();
        else while (std::isdigit(static_cast<unsigned char>(peek()))) take();
        if (peek() == '.') {
            take();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("bad number: digit expected after '.'");
            while (std::isdigit(static_cast<unsigned char>(peek()))) take();
        }
        if (peek() == 'e' || peek() == 'E') {
            take();
            if (peek() == '+' || peek() == '-') take();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("bad number: digit expected in exponent");
            while (std::isdigit(static_cast<unsigned char>(peek()))) take();
        }
        Value v;
        v.kind = Value::Kind::Number;
        v.raw = text_.substr(start, pos_ - start);
        v.number = std::strtod(v.raw.c_str(), nullptr);
        return v;
    }
};

}

Value parse(const std::string& text, const std::string& source) {
    Parser p(text, source);
    return p.parse_document();
}

}
