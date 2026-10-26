// Minimal XML reader (elements, attributes, text, comments, <?xml?>, 5 predefined entities).
// Enough for weapon.xml; deliberately not a general XML parser (no DTD, namespaces, CDATA).
#pragma once
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace wl {
struct XmlNode {
    std::string name, text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<XmlNode>> children;
    const XmlNode* child(const std::string& n) const {
        for (auto& c : children) if (c->name == n) return c.get();
        return nullptr;
    }
    const std::string* attr(const std::string& n) const {
        for (auto& a : attrs) if (a.first == n) return &a.second;
        return nullptr;
    }
};

class XmlParser {
public:
    // Returns nullptr and fills err ("line N: msg") on failure.
    static std::unique_ptr<XmlNode> parse(const std::string& s, std::string& err) {
        XmlParser p(s);
        auto root = p.document();
        if (!root) err = "line " + std::to_string(p.line_) + ": " + p.err_;
        return root;
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    int line_ = 1;
    std::string err_;
    explicit XmlParser(const std::string& s) : s_(s) {}

    bool eof() const { return i_ >= s_.size(); }
    char cur() const { return s_[i_]; }
    void adv() { if (s_[i_] == '\n') ++line_; ++i_; }
    bool starts(const char* t) const { return s_.compare(i_, std::char_traits<char>::length(t), t) == 0; }
    void ws() { while (!eof() && std::isspace((unsigned char)cur())) adv(); }
    bool skipUntil(const char* t) {
        size_t n = std::char_traits<char>::length(t);
        while (!eof() && !starts(t)) adv();
        if (eof()) return false;
        for (size_t k = 0; k < n; ++k) adv();
        return true;
    }
    void misc() {
        for (;;) {
            ws();
            if (starts("<?")) { if (!skipUntil("?>")) { err_ = "unterminated <?...?>"; return; } }
            else if (starts("<!--")) { if (!skipUntil("-->")) { err_ = "unterminated comment"; return; } }
            else return;
        }
    }
    static std::string decode(const std::string& t) {
        std::string o;
        for (size_t k = 0; k < t.size();) {
            if (t[k] == '&') {
                if (t.compare(k, 5, "&amp;") == 0) { o += '&'; k += 5; continue; }
                if (t.compare(k, 4, "&lt;") == 0) { o += '<'; k += 4; continue; }
                if (t.compare(k, 4, "&gt;") == 0) { o += '>'; k += 4; continue; }
                if (t.compare(k, 6, "&quot;") == 0) { o += '"'; k += 6; continue; }
                if (t.compare(k, 6, "&apos;") == 0) { o += '\''; k += 6; continue; }
            }
            o += t[k++];
        }
        return o;
    }
    static bool nameCh(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':' || c == '.'; }
    static std::string trim(const std::string& t) {
        size_t a = 0, b = t.size();
        while (a < b && std::isspace((unsigned char)t[a])) ++a;
        while (b > a && std::isspace((unsigned char)t[b - 1])) --b;
        return t.substr(a, b - a);
    }

    std::unique_ptr<XmlNode> document() {
        misc(); if (!err_.empty()) return nullptr;
        if (eof() || cur() != '<') { err_ = "expected root element"; return nullptr; }
        auto r = element(0); if (!r) return nullptr;
        misc(); if (!err_.empty()) return nullptr;
        if (!eof()) { err_ = "content after root element"; return nullptr; }
        return r;
    }

    std::unique_ptr<XmlNode> element(int depth) {
        if (depth > 32) { err_ = "nesting too deep"; return nullptr; }
        adv(); // '<'
        auto n = std::make_unique<XmlNode>();
        while (!eof() && nameCh(cur())) { n->name += cur(); adv(); }
        if (n->name.empty()) { err_ = "empty element name"; return nullptr; }
        for (;;) {
            ws();
            if (eof()) { err_ = "unexpected end inside <" + n->name + ">"; return nullptr; }
            if (starts("/>")) { adv(); adv(); return n; }
            if (cur() == '>') { adv(); break; }
            std::string k, v;
            while (!eof() && nameCh(cur())) { k += cur(); adv(); }
            if (k.empty()) { err_ = "bad attribute in <" + n->name + ">"; return nullptr; }
            ws();
            if (eof() || cur() != '=') { err_ = "attribute '" + k + "' missing '='"; return nullptr; }
            adv(); ws();
            if (eof() || (cur() != '"' && cur() != '\'')) { err_ = "attribute '" + k + "' value not quoted"; return nullptr; }
            char q = cur(); adv();
            while (!eof() && cur() != q) { v += cur(); adv(); }
            if (eof()) { err_ = "unterminated attribute value"; return nullptr; }
            adv();
            n->attrs.emplace_back(k, decode(v));
        }
        for (;;) {
            if (eof()) { err_ = "missing </" + n->name + ">"; return nullptr; }
            if (starts("</")) {
                adv(); adv();
                std::string e;
                while (!eof() && nameCh(cur())) { e += cur(); adv(); }
                ws();
                if (eof() || cur() != '>') { err_ = "malformed closing tag"; return nullptr; }
                adv();
                if (e != n->name) { err_ = "mismatched </" + e + "> for <" + n->name + ">"; return nullptr; }
                n->text = trim(decode(n->text));
                return n;
            }
            if (starts("<!--")) { if (!skipUntil("-->")) { err_ = "unterminated comment"; return nullptr; } continue; }
            if (cur() == '<') {
                auto c = element(depth + 1);
                if (!c) return nullptr;
                n->children.push_back(std::move(c));
                continue;
            }
            n->text += cur(); adv();
        }
    }
};
} // namespace wl
