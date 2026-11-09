#include "providers/dgg/DggEmoteCss.hpp"

#include "providers/dgg/DggCssAnimation.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace chatterino {

using namespace dggcss;

namespace {

using std::string;
using std::string_view;

constexpr size_t NPOS = string::npos;

/// An animation with more steps than this isn't a sprite animation we want
/// to cut up (and it would need an absurd amount of memory).
constexpr int MAX_STEPS = 1000;

using Props = std::unordered_map<string, string>;
using Keyframes =
    std::unordered_map<string, std::vector<dggcss::KeyframeStop>>;

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool isIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
           c == '-' || static_cast<unsigned char>(c) >= 0x80;
}

string_view trimView(string_view s)
{
    while (!s.empty() && isSpace(s.front()))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && isSpace(s.back()))
    {
        s.remove_suffix(1);
    }
    return s;
}

string toLower(string_view s)
{
    string out(s);
    for (auto &c : out)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool startsWith(string_view s, string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

bool contains(const std::vector<string> &list, string_view needle)
{
    return std::find(list.begin(), list.end(), needle) != list.end();
}

/// Splits at `sep`, but not inside quotes, parentheses or brackets.
std::vector<string> splitTopLevel(string_view s, char sep)
{
    std::vector<string> out;
    string cur;
    int depth = 0;
    char quote = 0;

    for (const char c : s)
    {
        if (quote != 0)
        {
            cur.push_back(c);
            if (c == quote)
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            cur.push_back(c);
            continue;
        }
        if (c == '(' || c == '[')
        {
            depth++;
        }
        else if (c == ')' || c == ']')
        {
            depth--;
        }

        if (c == sep && depth <= 0)
        {
            out.push_back(std::move(cur));
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    out.push_back(std::move(cur));
    return out;
}

/// Splits at whitespace, but not inside quotes or parentheses.
std::vector<string> splitWhitespace(string_view s)
{
    std::vector<string> out;
    string cur;
    int depth = 0;
    char quote = 0;

    for (const char c : s)
    {
        if (quote != 0)
        {
            cur.push_back(c);
            if (c == quote)
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            cur.push_back(c);
            continue;
        }
        if (c == '(')
        {
            depth++;
        }
        else if (c == ')')
        {
            depth--;
        }

        if (isSpace(c) && depth <= 0)
        {
            if (!cur.empty())
            {
                out.push_back(std::move(cur));
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        out.push_back(std::move(cur));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

/// Parses a number starting at `pos` and moves `pos` behind it.
std::optional<double> parseNumberAt(string_view s, size_t &pos)
{
    size_t start = pos;
    if (start < s.size() && s[start] == '+')
    {
        // from_chars doesn't accept a leading plus
        start++;
    }

    size_t probe = start;
    if (probe < s.size() && s[probe] == '-')
    {
        probe++;
    }
    if (probe >= s.size() || !(isDigit(s[probe]) || s[probe] == '.'))
    {
        // also keeps from_chars from reading "inf" and "nan"
        return std::nullopt;
    }

    double value = 0;
    const auto [end, ec] =
        std::from_chars(s.data() + start, s.data() + s.size(), value);
    if (ec != std::errc())
    {
        return std::nullopt;
    }
    pos = static_cast<size_t>(end - s.data());
    return value;
}

/// A bare number ("1.25", "3").
std::optional<double> parseNumber(string_view s)
{
    s = trimView(s);
    size_t pos = 0;
    auto value = parseNumberAt(s, pos);
    if (!value || pos != s.size())
    {
        return std::nullopt;
    }
    return value;
}

/// `12px`, `-0.5px`, or a bare `0`.
std::optional<double> parseLengthPx(string_view s)
{
    s = trimView(s);
    size_t pos = 0;
    auto value = parseNumberAt(s, pos);
    if (!value)
    {
        return std::nullopt;
    }
    const auto unit = s.substr(pos);
    if (unit == "px" || (unit.empty() && *value == 0.0))
    {
        return value;
    }
    return std::nullopt;
}

/// `1.3s`, `.67s`, `250ms`, in milliseconds.
std::optional<double> parseTimeMs(string_view s)
{
    s = trimView(s);
    size_t pos = 0;
    auto value = parseNumberAt(s, pos);
    if (!value)
    {
        return std::nullopt;
    }
    const auto unit = s.substr(pos);
    if (unit == "s")
    {
        return *value * 1000.0;
    }
    if (unit == "ms")
    {
        return *value;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Block scanning
// ---------------------------------------------------------------------------

string stripComments(string_view css)
{
    string out;
    out.reserve(css.size());

    size_t i = 0;
    while (i < css.size())
    {
        if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*')
        {
            const auto end = css.find("*/", i + 2);
            if (end == NPOS)
            {
                break;  // unterminated comment swallows the rest
            }
            i = end + 2;
            out.push_back(' ');
            continue;
        }
        if (css[i] != '\r')
        {
            out.push_back(css[i]);
        }
        i++;
    }
    return out;
}

/// The index of the `}` that closes the `{` at `open`, or NPOS.
size_t findClosingBrace(string_view s, size_t open)
{
    int depth = 0;
    char quote = 0;
    for (size_t i = open; i < s.size(); i++)
    {
        const char c = s[i];
        if (quote != 0)
        {
            if (c == '\\')
            {
                i++;
            }
            else if (c == quote)
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
        }
        else if (c == '{')
        {
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0)
            {
                return i;
            }
        }
    }
    return NPOS;
}

/// Calls `fn(prelude, body)` for each `prelude { body }` in `css`. Statements
/// like `@import ...;` and stray text are skipped.
template <typename Fn>
void forEachBlock(string_view css, Fn &&fn)
{
    size_t i = 0;
    while (i < css.size())
    {
        const auto brace = css.find('{', i);
        if (brace == NPOS)
        {
            break;
        }
        const auto semicolon = css.find(';', i);
        if (semicolon != NPOS && semicolon < brace)
        {
            i = semicolon + 1;
            continue;
        }

        const auto close = findClosingBrace(css, brace);
        if (close == NPOS)
        {
            break;
        }
        fn(trimView(css.substr(i, brace - i)),
           css.substr(brace + 1, close - brace - 1));
        i = close + 1;
    }
}

std::vector<Decl> parseDeclarations(string_view body)
{
    std::vector<Decl> out;
    for (const auto &part : splitTopLevel(body, ';'))
    {
        const auto colon = part.find(':');
        if (colon == NPOS)
        {
            continue;
        }

        const auto name = trimView(string_view(part).substr(0, colon));
        auto value = trimView(string_view(part).substr(colon + 1));
        if (name.empty() || startsWith(name, "//"))
        {
            // commented out with `//`, which isn't CSS but is in there
            continue;
        }

        Decl decl;
        decl.name = startsWith(name, "--") ? string(name) : toLower(name);

        const auto bang = value.rfind('!');
        if (bang != NPOS &&
            toLower(trimView(value.substr(bang + 1))) == "important")
        {
            decl.important = true;
            value = trimView(value.substr(0, bang));
        }
        decl.value = string(value);
        out.push_back(std::move(decl));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Selectors
// ---------------------------------------------------------------------------

struct RawPart {
    char combinator;
    string text;
};

std::vector<RawPart> tokenizeSelector(string_view sel)
{
    std::vector<RawPart> out;
    string cur;
    char comb = 0;
    int depth = 0;
    char quote = 0;

    for (const char c : sel)
    {
        if (quote != 0)
        {
            cur.push_back(c);
            if (c == quote)
            {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            cur.push_back(c);
            continue;
        }
        if (c == '(' || c == '[')
        {
            depth++;
        }
        else if (c == ')' || c == ']')
        {
            depth--;
        }

        const bool combinator = c == '>' || c == '+' || c == '~';
        if (depth == 0 && (isSpace(c) || combinator))
        {
            if (!cur.empty())
            {
                out.push_back(RawPart{comb, std::move(cur)});
                cur.clear();
                comb = 0;
            }
            if (combinator)
            {
                comb = c;
            }
            else if (comb == 0)
            {
                comb = ' ';
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty())
    {
        out.push_back(RawPart{comb, std::move(cur)});
    }
    return out;
}

std::optional<Attr> parseAttr(string_view inner)
{
    inner = trimView(inner);
    size_t i = 0;

    Attr attr;
    while (i < inner.size() && isIdentChar(inner[i]))
    {
        attr.name.push_back(inner[i]);
        i++;
    }
    if (attr.name.empty())
    {
        return std::nullopt;
    }
    while (i < inner.size() && isSpace(inner[i]))
    {
        i++;
    }
    if (i == inner.size())
    {
        return attr;
    }

    while (i < inner.size() && string_view("~|^$*=").find(inner[i]) != NPOS)
    {
        attr.op.push_back(inner[i]);
        i++;
    }
    if (attr.op.empty())
    {
        return std::nullopt;
    }
    while (i < inner.size() && isSpace(inner[i]))
    {
        i++;
    }
    if (i == inner.size())
    {
        return std::nullopt;
    }

    if (inner[i] == '"' || inner[i] == '\'')
    {
        const char quote = inner[i];
        const auto end = inner.find(quote, i + 1);
        if (end == NPOS)
        {
            return std::nullopt;
        }
        attr.value = string(inner.substr(i + 1, end - i - 1));
    }
    else
    {
        while (i < inner.size() && !isSpace(inner[i]))
        {
            attr.value.push_back(inner[i]);
            i++;
        }
    }
    return attr;
}

std::optional<Compound> parseCompound(string_view c)
{
    Compound out;
    size_t i = 0;
    const size_t n = c.size();
    if (i < n && c[i] == '*')
    {
        i++;
    }

    while (i < n)
    {
        const char ch = c[i];
        if (ch == '.')
        {
            i++;
            string name;
            while (i < n && (isIdentChar(c[i]) || c[i] == '\\'))
            {
                if (c[i] == '\\' && i + 1 < n)
                {
                    i++;  // an escaped character stands for itself
                }
                name.push_back(c[i]);
                i++;
            }
            if (name.empty())
            {
                return std::nullopt;
            }
            out.classes.push_back(std::move(name));
        }
        else if (ch == '[')
        {
            size_t j = i + 1;
            char quote = 0;
            for (; j < n; j++)
            {
                const char d = c[j];
                if (quote != 0)
                {
                    if (d == quote)
                    {
                        quote = 0;
                    }
                    continue;
                }
                if (d == '"' || d == '\'')
                {
                    quote = d;
                }
                else if (d == ']')
                {
                    break;
                }
            }
            if (j >= n)
            {
                return std::nullopt;
            }
            auto attr = parseAttr(c.substr(i + 1, j - i - 1));
            if (!attr)
            {
                return std::nullopt;
            }
            out.attrs.push_back(std::move(*attr));
            i = j + 1;
        }
        else if (ch == ':')
        {
            size_t j = i + 1;
            if (j < n && c[j] == ':')
            {
                j++;
            }
            const size_t nameStart = j;
            while (j < n && isIdentChar(c[j]))
            {
                j++;
            }
            if (j == nameStart)
            {
                return std::nullopt;
            }
            if (j < n && c[j] == '(')
            {
                int depth = 0;
                do
                {
                    if (c[j] == '(')
                    {
                        depth++;
                    }
                    else if (c[j] == ')')
                    {
                        depth--;
                    }
                    j++;
                } while (j < n && depth > 0);
                if (depth != 0)
                {
                    return std::nullopt;
                }
            }
            out.pseudos.emplace_back(c.substr(i, j - i));
            i = j;
        }
        else
        {
            return std::nullopt;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pseudo-classes that look at the message's elements
// ---------------------------------------------------------------------------

/// `odd`, `even`, `3`, `2n`, `-n+2`, `6n-5` as `an + b`.
std::optional<std::pair<int, int>> parseNth(string_view raw)
{
    string text;
    for (const char c : raw)
    {
        if (!isSpace(c))
        {
            text.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(c))));
        }
    }
    if (text == "odd")
    {
        return std::make_pair(2, 1);
    }
    if (text == "even")
    {
        return std::make_pair(2, 0);
    }
    if (text.empty())
    {
        return std::nullopt;
    }

    const auto toInt = [](string_view part) -> std::optional<int> {
        if (part.empty())
        {
            return std::nullopt;
        }
        if (part.front() == '+')
        {
            part.remove_prefix(1);
        }
        int value = 0;
        const auto *end = part.data() + part.size();
        const auto result = std::from_chars(part.data(), end, value);
        if (result.ec != std::errc() || result.ptr != end)
        {
            return std::nullopt;
        }
        return value;
    };

    const auto n = text.find('n');
    if (n == NPOS)
    {
        if (const auto b = toInt(text))
        {
            return std::make_pair(0, *b);
        }
        return std::nullopt;
    }

    const string aText = text.substr(0, n);
    int a = 1;
    if (aText == "-")
    {
        a = -1;
    }
    else if (!aText.empty() && aText != "+")
    {
        const auto parsed = toInt(aText);
        if (!parsed)
        {
            return std::nullopt;
        }
        a = *parsed;
    }

    int b = 0;
    if (n + 1 < text.size())
    {
        const auto parsed = toInt(string_view(text).substr(n + 1));
        if (!parsed)
        {
            return std::nullopt;
        }
        b = *parsed;
    }
    return std::make_pair(a, b);
}

bool fillStructural(Compound &c, string &why);

std::optional<ComplexSelector> parseComplex(string_view text, string &why)
{
    ComplexSelector out;
    for (const auto &raw : tokenizeSelector(text))
    {
        auto compound = parseCompound(raw.text);
        if (!compound)
        {
            why = "unparsable selector";
            return std::nullopt;
        }
        if (!fillStructural(*compound, why))
        {
            return std::nullopt;
        }
        out.parts.push_back(SelectorPart{raw.combinator, std::move(*compound)});
    }
    if (out.parts.empty())
    {
        why = "empty selector";
        return std::nullopt;
    }
    return out;
}

/// Parses the pseudo-classes of a compound into `structural`. Returns false
/// (with `why` set) for one that isn't understood. `:hover` is left to the
/// caller, a pseudo-element is an error.
bool fillStructural(Compound &c, string &why)
{
    using Kind = PseudoClass::Kind;

    c.structural.clear();
    for (const auto &pseudo : c.pseudos)
    {
        if (pseudo == ":hover")
        {
            continue;
        }
        if (startsWith(pseudo, "::") || pseudo == ":before" ||
            pseudo == ":after")
        {
            why = "pseudo-element";
            return false;
        }

        const auto paren = pseudo.find('(');
        const string name = toLower(
            string_view(pseudo).substr(1, (paren == NPOS ? pseudo.size() : paren) - 1));
        string_view arg;
        if (paren != NPOS && pseudo.back() == ')')
        {
            arg = string_view(pseudo).substr(paren + 1,
                                             pseudo.size() - paren - 2);
        }

        PseudoClass pc;
        if (name == "not" || name == "is" || name == "where" || name == "has")
        {
            if (paren == NPOS)
            {
                why = "pseudo-class " + name;
                return false;
            }
            pc.kind = name == "not"   ? Kind::Not
                      : name == "has" ? Kind::Has
                                      : Kind::Is;
            for (const auto &part : splitTopLevel(arg, ','))
            {
                auto complex = parseComplex(trimView(part), why);
                if (!complex)
                {
                    return false;
                }
                if (pc.kind != Kind::Has)
                {
                    complex->parts.front().combinator = 0;
                }
                pc.args.push_back(std::move(*complex));
            }
            if (pc.args.empty())
            {
                why = "pseudo-class " + name;
                return false;
            }
        }
        else if (name == "first-child")
        {
            pc.kind = Kind::FirstChild;
        }
        else if (name == "last-child")
        {
            pc.kind = Kind::LastChild;
        }
        else if (name == "only-child")
        {
            pc.kind = Kind::OnlyChild;
        }
        else if (name == "first-of-type")
        {
            pc.kind = Kind::FirstOfType;
        }
        else if (name == "last-of-type")
        {
            pc.kind = Kind::LastOfType;
        }
        else if (name == "only-of-type")
        {
            pc.kind = Kind::OnlyOfType;
        }
        else if (name == "nth-child" || name == "nth-last-child" ||
                 name == "nth-of-type" || name == "nth-last-of-type")
        {
            const auto nth = paren == NPOS ? std::nullopt : parseNth(arg);
            if (!nth)
            {
                why = "pseudo-class " + name;
                return false;
            }
            pc.kind = name == "nth-child"        ? Kind::NthChild
                      : name == "nth-last-child" ? Kind::NthLastChild
                      : name == "nth-of-type"    ? Kind::NthOfType
                                                 : Kind::NthLastOfType;
            pc.a = nth->first;
            pc.b = nth->second;
        }
        else
        {
            why = "pseudo-class " + name;
            return false;
        }
        c.structural.push_back(std::move(pc));
    }
    return true;
}

int complexSpecificity(const ComplexSelector &complex);

/// Classes, attributes and pseudo-classes count one each; `:not()`, `:is()`
/// and `:has()` count like their most specific argument.
int compoundSpecificity(const Compound &c)
{
    int n = static_cast<int>(c.classes.size() + c.attrs.size());
    for (const auto &pseudo : c.pseudos)
    {
        if (pseudo == ":hover")
        {
            n++;
        }
    }
    for (const auto &pc : c.structural)
    {
        if (pc.args.empty())
        {
            n++;
            continue;
        }
        int best = 0;
        for (const auto &arg : pc.args)
        {
            best = std::max(best, complexSpecificity(arg));
        }
        n += best;
    }
    return n;
}

int complexSpecificity(const ComplexSelector &complex)
{
    int n = 0;
    for (const auto &part : complex.parts)
    {
        n += compoundSpecificity(part.compound);
    }
    return n;
}

struct Analysis {
    /// Empty if the selector is supported, else why not.
    string rejected;

    string emote;
    std::vector<SelectorPart> parts;
    int specificity = 0;
    bool hover = false;
    bool dependsOnContext = false;
};

/// Checks that a selector has the shape DGG's per-emote rules have, and
/// extracts what's needed to evaluate it.
Analysis analyseSelector(string_view selector)
{
    Analysis out;
    const auto raw = tokenizeSelector(selector);
    if (raw.empty())
    {
        out.rejected = "empty selector";
        return out;
    }

    for (const auto &part : raw)
    {
        auto compound = parseCompound(part.text);
        if (!compound)
        {
            out.rejected = "unparsable selector";
            return out;
        }
        out.parts.push_back(SelectorPart{part.combinator, std::move(*compound)});
    }
    out.parts.front().combinator = 0;

    // The subject: `.emote.NAME` (a class can be repeated to make the rule
    // more specific)
    const auto &subject = out.parts.back().compound;
    string subjectEmote;
    bool subjectOk = subject.attrs.empty() && contains(subject.classes, "emote");
    for (const auto &cls : subject.classes)
    {
        if (cls == "emote")
        {
            continue;
        }
        if (subjectEmote.empty())
        {
            subjectEmote = cls;
        }
        else if (subjectEmote != cls)
        {
            subjectOk = false;
        }
    }
    if (!subjectOk || subjectEmote.empty())
    {
        out.rejected = "selector isn't for a single emote";
        return out;
    }
    out.emote = subjectEmote;

    for (auto &part : out.parts)
    {
        string why;
        if (!fillStructural(part.compound, why))
        {
            out.rejected = why;
            return out;
        }
        for (const auto &pseudo : part.compound.pseudos)
        {
            if (pseudo == ":hover")
            {
                out.hover = true;
            }
        }
        if (!part.compound.structural.empty())
        {
            out.dependsOnContext = true;
        }
    }

    // Everything above the subject must be a part of chat-gui's message DOM
    // that we know: the message, the nick, the text, or another emote.
    for (size_t i = 0; i + 1 < out.parts.size(); i++)
    {
        const auto &c = out.parts[i].compound;
        const bool isEmote = contains(c.classes, "emote");
        const bool isGreentext = contains(c.classes, "greentext");
        const bool isMessage =
            contains(c.classes, "msg-chat") ||
            (c.classes.empty() && !c.attrs.empty());
        const bool isUser = contains(c.classes, "user");
        const bool isText = c.classes.size() == 1 && c.classes[0] == "text";

        if (isMessage && !isUser && !isText && !isEmote && !isGreentext)
        {
            for (const auto &attr : c.attrs)
            {
                if (attr.name != "data-username" || attr.op != "=")
                {
                    out.rejected = "attribute " + attr.name;
                    return out;
                }
            }
            if (contains(c.classes, "msg-emote"))
            {
                out.rejected = "combo message";
                return out;
            }
            if (contains(c.classes, "text"))
            {
                out.rejected = "selector context";
                return out;
            }
            if (!c.attrs.empty())
            {
                out.dependsOnContext = true;
            }
            for (const auto &cls : c.classes)
            {
                if (cls != "msg-chat" && cls != "msg-user")
                {
                    out.dependsOnContext = true;
                }
            }
        }
        else if (isEmote && !isMessage && !isUser && !isText &&
                 !isGreentext && c.attrs.empty())
        {
            // another emote of the message
            out.dependsOnContext = true;
        }
        else if (isUser && !isMessage && !isEmote && c.attrs.empty())
        {
            if (c.classes.size() > 1)
            {
                out.dependsOnContext = true;
            }
        }
        else if (isText && c.attrs.empty())
        {
            // always there
        }
        else if (isGreentext && c.classes.size() == 1 && c.attrs.empty())
        {
            // only there in greentext
            out.dependsOnContext = true;
        }
        else
        {
            out.rejected = "selector context";
            return out;
        }
    }

    for (size_t i = 1; i < out.parts.size(); i++)
    {
        const char comb = out.parts[i].combinator;
        if (comb == '+' || (comb == '~' &&
                            contains(out.parts[i - 1].compound.classes, "emote")))
        {
            out.dependsOnContext = true;
        }
    }

    for (const auto &part : out.parts)
    {
        out.specificity += compoundSpecificity(part.compound);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Matching against a chat-gui message
// ---------------------------------------------------------------------------

/// The elements of a message that selectors can talk about:
/// message (M) > [nick (U), text (T) > [greentext (G) >] emotes (E) and
/// links (L)].
struct Node {
    char kind = 0;
    /// The element name, which `:*-of-type` counts by.
    char tag = 0;
    std::vector<string> classes;
    string username;
    int parent = -1;
    /// Position among the parent's children.
    int pos = 0;
};

struct Dom {
    std::vector<Node> nodes;
    std::vector<std::vector<int>> children;
    int emote = -1;
};

int addNode(Dom &dom, Node node, int parent)
{
    const int index = static_cast<int>(dom.nodes.size());
    node.parent = parent;
    if (parent >= 0)
    {
        auto &siblings = dom.children[static_cast<size_t>(parent)];
        node.pos = static_cast<int>(siblings.size());
        siblings.push_back(index);
    }
    dom.nodes.push_back(std::move(node));
    dom.children.emplace_back();
    return index;
}

Dom buildDom(const string &emote, const DggEmoteContext &ctx)
{
    Dom dom;

    Node m;
    m.kind = 'M';
    m.tag = 'd';
    m.classes = {"msg-chat", "msg-user"};
    for (const auto &cls : ctx.messageClasses)
    {
        m.classes.push_back(cls.toStdString());
    }
    m.username = ctx.username.toStdString();
    const int mIndex = addNode(dom, std::move(m), -1);

    Node u;
    u.kind = 'U';
    u.tag = 'a';
    u.classes = {"user"};
    if (!ctx.nickColorFlair.isEmpty())
    {
        u.classes.push_back(ctx.nickColorFlair.toStdString());
    }
    addNode(dom, std::move(u), mIndex);

    Node t;
    t.kind = 'T';
    t.tag = 's';
    t.classes = {"text"};
    int container = addNode(dom, std::move(t), mIndex);

    if (ctx.greentext)
    {
        Node g;
        g.kind = 'G';
        g.tag = 's';
        g.classes = {"greentext"};
        container = addNode(dom, std::move(g), container);
    }

    // The elements of the text. Without any information about the message
    // the emote is the only one.
    QStringList elements = ctx.elements;
    int index = ctx.elementIndex;
    const QString name = QString::fromStdString(emote);
    if (index < 0 || index >= elements.size() || elements[index] != name)
    {
        elements = QStringList{name};
        index = 0;
    }
    for (int i = 0; i < elements.size(); i++)
    {
        Node e;
        if (elements[i].isEmpty())
        {
            e.kind = 'L';
            e.tag = 'a';
            e.classes = {"externallink"};
        }
        else
        {
            e.kind = 'E';
            e.tag = 'd';
            e.classes = {"emote", elements[i].toStdString()};
        }
        const int node = addNode(dom, std::move(e), container);
        if (i == index)
        {
            dom.emote = node;
        }
    }
    return dom;
}

bool isSubset(const std::vector<string> &wanted, const std::vector<string> &have)
{
    return std::all_of(wanted.begin(), wanted.end(),
                       [&](const string &cls) { return contains(have, cls); });
}

bool compoundMatches(const Compound &c, const Dom &dom, int nodeIndex);
bool matchChain(const std::vector<SelectorPart> &parts, size_t index,
                const Dom &dom, int nodeIndex);

/// The nodes that share a parent with `nodeIndex` (itself included).
std::vector<int> siblingsOf(const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    if (node.parent < 0)
    {
        return {nodeIndex};
    }
    return dom.children[static_cast<size_t>(node.parent)];
}

bool nthMatches(int a, int b, int position)
{
    if (a == 0)
    {
        return position == b;
    }
    const int diff = position - b;
    return diff % a == 0 && diff / a >= 0;
}

/// Whether anything matching `parts[i..]` hangs off `from` the way the
/// combinators say (the first one is the leading combinator of a `:has()`).
bool hasMatch(const std::vector<SelectorPart> &parts, size_t i, const Dom &dom,
              int from)
{
    char comb = parts[i].combinator;
    if (comb == 0)
    {
        comb = ' ';
    }

    std::vector<int> candidates;
    const Node &node = dom.nodes[static_cast<size_t>(from)];
    switch (comb)
    {
        case '+':
        case '~': {
            const auto siblings = siblingsOf(dom, from);
            const size_t pos = static_cast<size_t>(node.pos);
            if (node.parent >= 0)
            {
                if (comb == '+' && pos + 1 < siblings.size())
                {
                    candidates.push_back(siblings[pos + 1]);
                }
                else if (comb == '~')
                {
                    for (size_t k = pos + 1; k < siblings.size(); k++)
                    {
                        candidates.push_back(siblings[k]);
                    }
                }
            }
            break;
        }
        case '>':
            candidates = dom.children[static_cast<size_t>(from)];
            break;
        default: {
            std::vector<int> stack = dom.children[static_cast<size_t>(from)];
            while (!stack.empty())
            {
                const int next = stack.back();
                stack.pop_back();
                candidates.push_back(next);
                for (const int child : dom.children[static_cast<size_t>(next)])
                {
                    stack.push_back(child);
                }
            }
            break;
        }
    }

    for (const int candidate : candidates)
    {
        if (!compoundMatches(parts[i].compound, dom, candidate))
        {
            continue;
        }
        if (i + 1 == parts.size() || hasMatch(parts, i + 1, dom, candidate))
        {
            return true;
        }
    }
    return false;
}

bool evalPseudo(const PseudoClass &pc, const Dom &dom, int nodeIndex)
{
    using Kind = PseudoClass::Kind;

    switch (pc.kind)
    {
        case Kind::Not:
            return std::none_of(
                pc.args.begin(), pc.args.end(), [&](const ComplexSelector &a) {
                    return matchChain(a.parts, a.parts.size() - 1, dom,
                                      nodeIndex);
                });
        case Kind::Is:
            return std::any_of(
                pc.args.begin(), pc.args.end(), [&](const ComplexSelector &a) {
                    return matchChain(a.parts, a.parts.size() - 1, dom,
                                      nodeIndex);
                });
        case Kind::Has:
            return std::any_of(
                pc.args.begin(), pc.args.end(), [&](const ComplexSelector &a) {
                    return hasMatch(a.parts, 0, dom, nodeIndex);
                });
        default:
            break;
    }

    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    const auto siblings = siblingsOf(dom, nodeIndex);
    int position = 1;  // 1-based, among all siblings
    int count = static_cast<int>(siblings.size());
    int typedPosition = 0;
    int typedCount = 0;
    for (const int sibling : siblings)
    {
        const bool sameType = dom.nodes[static_cast<size_t>(sibling)].tag ==
                              node.tag;
        if (sameType)
        {
            typedCount++;
        }
        if (sibling == nodeIndex)
        {
            position = static_cast<int>(&sibling - siblings.data()) + 1;
            typedPosition = typedCount;
        }
    }

    switch (pc.kind)
    {
        case Kind::FirstChild:
            return position == 1;
        case Kind::LastChild:
            return position == count;
        case Kind::OnlyChild:
            return count == 1;
        case Kind::FirstOfType:
            return typedPosition == 1;
        case Kind::LastOfType:
            return typedPosition == typedCount;
        case Kind::OnlyOfType:
            return typedCount == 1;
        case Kind::NthChild:
            return nthMatches(pc.a, pc.b, position);
        case Kind::NthLastChild:
            return nthMatches(pc.a, pc.b, count - position + 1);
        case Kind::NthOfType:
            return nthMatches(pc.a, pc.b, typedPosition);
        case Kind::NthLastOfType:
            return nthMatches(pc.a, pc.b, typedCount - typedPosition + 1);
        default:
            return false;
    }
}

bool compoundMatches(const Compound &c, const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];

    bool matches = false;
    switch (node.kind)
    {
        case 'M': {
            matches = true;
            if (c.classes.empty() && c.attrs.empty())
            {
                matches = false;
            }
            else if (contains(c.classes, "user") ||
                     contains(c.classes, "text") ||
                     contains(c.classes, "emote") ||
                     contains(c.classes, "greentext"))
            {
                matches = false;
            }
            else if (!isSubset(c.classes, node.classes))
            {
                matches = false;
            }
            else
            {
                for (const auto &attr : c.attrs)
                {
                    if (attr.name != "data-username" || attr.op != "=" ||
                        attr.value != node.username)
                    {
                        matches = false;
                        break;
                    }
                }
            }
            break;
        }
        case 'U':
            matches = contains(c.classes, "user") && c.attrs.empty() &&
                      isSubset(c.classes, node.classes);
            break;
        case 'T':
            matches = c.classes.size() == 1 && c.classes[0] == "text" &&
                      c.attrs.empty();
            break;
        case 'G':
            matches = c.classes.size() == 1 && c.classes[0] == "greentext" &&
                      c.attrs.empty();
            break;
        case 'E':
        case 'L':
            matches = c.attrs.empty() && isSubset(c.classes, node.classes);
            break;
        default:
            break;
    }
    if (!matches)
    {
        return false;
    }

    return std::all_of(c.structural.begin(), c.structural.end(),
                       [&](const PseudoClass &pc) {
                           return evalPseudo(pc, dom, nodeIndex);
                       });
}

bool matchChain(const std::vector<SelectorPart> &parts, size_t index,
                const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    if (!compoundMatches(parts[index].compound, dom, nodeIndex))
    {
        return false;
    }
    if (index == 0)
    {
        return true;
    }

    switch (parts[index].combinator)
    {
        case ' ':
            for (int a = node.parent; a >= 0;
                 a = dom.nodes[static_cast<size_t>(a)].parent)
            {
                if (matchChain(parts, index - 1, dom, a))
                {
                    return true;
                }
            }
            return false;
        case '>':
            return node.parent >= 0 &&
                   matchChain(parts, index - 1, dom, node.parent);
        case '+': {
            if (node.parent < 0 || node.pos == 0)
            {
                return false;
            }
            const auto &siblings =
                dom.children[static_cast<size_t>(node.parent)];
            return matchChain(parts, index - 1, dom,
                              siblings[static_cast<size_t>(node.pos - 1)]);
        }
        case '~': {
            if (node.parent < 0)
            {
                return false;
            }
            const auto &siblings =
                dom.children[static_cast<size_t>(node.parent)];
            for (int k = 0; k < node.pos; k++)
            {
                if (matchChain(parts, index - 1, dom,
                               siblings[static_cast<size_t>(k)]))
                {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

/// Replaces `var(--x, fallback)`. Returns nullopt if a variable is neither
/// defined nor has a fallback.
std::optional<string> resolveVars(string_view value, const Props &props,
                                  int depth = 0)
{
    if (depth > 8)
    {
        return std::nullopt;
    }

    string out;
    size_t i = 0;
    while (i < value.size())
    {
        const auto at = value.find("var(", i);
        if (at == NPOS)
        {
            out.append(value.substr(i));
            break;
        }
        out.append(value.substr(i, at - i));

        // find the matching parenthesis
        size_t j = at + 4;
        int parens = 1;
        while (j < value.size() && parens > 0)
        {
            if (value[j] == '(')
            {
                parens++;
            }
            else if (value[j] == ')')
            {
                parens--;
            }
            j++;
        }
        if (parens != 0)
        {
            return std::nullopt;
        }

        const auto inner = value.substr(at + 4, j - 1 - (at + 4));
        const auto comma = inner.find(',');
        const auto name = trimView(inner.substr(0, comma));
        const auto found = props.find(string(name));
        std::optional<string> replacement;
        if (found != props.end())
        {
            replacement = resolveVars(found->second, props, depth + 1);
        }
        else if (comma != NPOS)
        {
            replacement = resolveVars(trimView(inner.substr(comma + 1)),
                                      props, depth + 1);
        }
        if (!replacement)
        {
            return std::nullopt;
        }
        out.append(*replacement);
        i = j;
    }
    return out;
}

/// A time, or `calc(<time> / <number>)` / `calc(<time> * <number>)`, in ms.
std::optional<double> evalDurationMs(string_view value, const Props &props)
{
    const auto resolved = resolveVars(value, props);
    if (!resolved)
    {
        return std::nullopt;
    }
    auto text = trimView(*resolved);

    if (auto plain = parseTimeMs(text))
    {
        return plain;
    }

    if (!startsWith(text, "calc(") || text.back() != ')')
    {
        return std::nullopt;
    }
    const auto inner = trimView(text.substr(5, text.size() - 6));
    const auto op = inner.find_first_of("/*");
    if (op == NPOS)
    {
        return std::nullopt;
    }
    const auto lhs = parseTimeMs(inner.substr(0, op));
    const auto rhs = parseNumber(inner.substr(op + 1));
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }
    if (inner[op] == '/')
    {
        if (*rhs == 0.0)
        {
            return std::nullopt;
        }
        return *lhs / *rhs;
    }
    return *lhs * *rhs;
}

struct Steps {
    int count = 0;
    /// `jump-end` (the default) or `jump-start`.
    bool jumpStart = false;
};

std::optional<Steps> parseSteps(string_view timing)
{
    timing = trimView(timing);
    if (timing == "step-start")
    {
        return Steps{1, true};
    }
    if (timing == "step-end")
    {
        return Steps{1, false};
    }
    if (!startsWith(timing, "steps(") || timing.back() != ')')
    {
        return std::nullopt;
    }

    const auto args = splitTopLevel(timing.substr(6, timing.size() - 7), ',');
    if (args.empty() || args.size() > 2)
    {
        return std::nullopt;
    }
    const auto count = parseNumber(args[0]);
    if (!count || *count < 1 || *count != std::floor(*count))
    {
        return std::nullopt;
    }

    Steps steps;
    steps.count = static_cast<int>(std::min(*count, 1e6));
    if (args.size() == 2)
    {
        const auto jump = toLower(trimView(args[1]));
        if (jump == "jump-start" || jump == "start")
        {
            steps.jumpStart = true;
        }
        else if (jump != "jump-end" && jump != "end")
        {
            return std::nullopt;  // jump-none and jump-both
        }
    }
    return steps;
}

/// `animation` is a shorthand: it sets every longhand, so whatever an earlier
/// rule set for the ones it doesn't mention is reset.
void expandAnimation(Props &props, string_view value)
{
    props["animation-name"] = "none";
    props["animation-duration"] = "0s";
    props["animation-timing-function"] = "ease";
    props["animation-iteration-count"] = "1";
    props["animation-fill-mode"] = "none";
    props["animation-direction"] = "normal";
    props["animation-delay"] = "0s";

    // only the first animation of a list is looked at
    const auto first = splitTopLevel(value, ',').front();

    bool durationSeen = false;
    for (const auto &token : splitWhitespace(first))
    {
        const auto lower = toLower(token);
        if (startsWith(lower, "calc(") || parseTimeMs(token))
        {
            if (!durationSeen)
            {
                props["animation-duration"] = token;
                durationSeen = true;
            }
            else
            {
                props["animation-delay"] = token;
            }
        }
        else if (startsWith(lower, "steps(") ||
                 startsWith(lower, "cubic-bezier(") || lower == "ease" ||
                 lower == "ease-in" || lower == "ease-out" ||
                 lower == "ease-in-out" || lower == "linear" ||
                 lower == "step-start" || lower == "step-end")
        {
            props["animation-timing-function"] = token;
        }
        else if (lower == "infinite" || parseNumber(token))
        {
            props["animation-iteration-count"] = token;
        }
        else if (lower == "forwards" || lower == "backwards" ||
                 lower == "both")
        {
            props["animation-fill-mode"] = lower;
        }
        else if (lower == "normal" || lower == "reverse" ||
                 lower == "alternate" || lower == "alternate-reverse")
        {
            props["animation-direction"] = lower;
        }
        else if (lower != "none")
        {
            props["animation-name"] = token;
        }
    }
}

void setProperty(Props &props, const string &name, const string &value)
{
    if (name == "animation")
    {
        expandAnimation(props, value);
        // the whole list, for the baking of transform/filter animations (the
        // expanded properties only keep the first animation)
        props["animation-raw"] = value;
        return;
    }
    if (name.rfind("animation-", 0) == 0 && name != "animation-raw")
    {
        // a longhand changes what the shorthand said, so the raw list is no
        // longer what applies
        props.erase("animation-raw");
    }

    if (name == "background-position")
    {
        const auto tokens = splitWhitespace(value);
        const auto isVertical = [](const string &t) {
            return t == "top" || t == "bottom";
        };
        const auto isHorizontal = [](const string &t) {
            return t == "left" || t == "right";
        };

        if (tokens.size() == 1)
        {
            // the missing one defaults to `center`
            if (isVertical(tokens[0]))
            {
                props["background-position-x"] = "50%";
                props["background-position-y"] = tokens[0];
            }
            else
            {
                props["background-position-x"] = tokens[0];
                props["background-position-y"] = "50%";
            }
        }
        else if (tokens.size() == 2)
        {
            if (isVertical(tokens[0]) || isHorizontal(tokens[1]))
            {
                props["background-position-x"] = tokens[1];
                props["background-position-y"] = tokens[0];
            }
            else
            {
                props["background-position-x"] = tokens[0];
                props["background-position-y"] = tokens[1];
            }
        }
        else
        {
            // edge offsets (`right 4px top 2px`) aren't used by DGG
            props["background-position-x"] = "unsupported";
            props["background-position-y"] = "unsupported";
        }
        return;
    }

    props[name] = value;
}

/// The value of `background-position` along one axis in pixels, as CSS
/// computes it: how far the image is moved right/down from its default
/// position. Keywords and percentages are relative to the free space,
/// `frame - sheet`.
std::optional<double> positionPx(string_view raw, const Props &props,
                                 double frame, double sheet)
{
    const auto resolved = resolveVars(raw, props);
    if (!resolved)
    {
        return std::nullopt;
    }
    const auto value = toLower(trimView(*resolved));

    double fraction = -1;
    if (value == "left" || value == "top")
    {
        fraction = 0;
    }
    else if (value == "center")
    {
        fraction = 0.5;
    }
    else if (value == "right" || value == "bottom")
    {
        fraction = 1;
    }
    else if (!value.empty() && value.back() == '%')
    {
        const auto percent =
            parseNumber(string_view(value).substr(0, value.size() - 1));
        if (!percent)
        {
            return std::nullopt;
        }
        fraction = *percent / 100.0;
    }

    if (fraction >= 0 || (!value.empty() && value.back() == '%'))
    {
        if (sheet <= 0)
        {
            return 0.0;  // size of the image not known
        }
        return (frame - sheet) * fraction;
    }
    return parseLengthPx(value);
}

void parseFilters(string_view raw, const Props &props,
                  std::vector<SpriteColorFilter> &out,
                  std::vector<string> &ignored)
{
    using Kind = SpriteColorFilter::Kind;

    const auto resolved = resolveVars(raw, props);
    if (!resolved)
    {
        ignored.emplace_back("filter");
        return;
    }
    const string_view value = trimView(*resolved);
    if (value == "none")
    {
        return;
    }

    size_t i = 0;
    while (i < value.size())
    {
        while (i < value.size() && isSpace(value[i]))
        {
            i++;
        }
        if (i >= value.size())
        {
            break;
        }

        const auto open = value.find('(', i);
        if (open == NPOS)
        {
            ignored.emplace_back("filter");
            return;
        }
        int depth = 0;
        size_t close = open;
        for (; close < value.size(); close++)
        {
            if (value[close] == '(')
            {
                depth++;
            }
            else if (value[close] == ')' && --depth == 0)
            {
                break;
            }
        }
        if (close >= value.size())
        {
            ignored.emplace_back("filter");
            return;
        }

        const auto name = toLower(trimView(value.substr(i, open - i)));
        const auto args = trimView(value.substr(open + 1, close - open - 1));
        i = close + 1;

        SpriteColorFilter filter;
        bool known = true;
        if (name == "brightness")
        {
            filter.kind = Kind::Brightness;
        }
        else if (name == "contrast")
        {
            filter.kind = Kind::Contrast;
        }
        else if (name == "saturate")
        {
            filter.kind = Kind::Saturate;
        }
        else if (name == "grayscale")
        {
            filter.kind = Kind::Grayscale;
        }
        else if (name == "sepia")
        {
            filter.kind = Kind::Sepia;
        }
        else if (name == "invert")
        {
            filter.kind = Kind::Invert;
        }
        else if (name == "opacity")
        {
            filter.kind = Kind::Opacity;
        }
        else if (name == "hue-rotate")
        {
            filter.kind = Kind::HueRotate;
        }
        else
        {
            known = false;
        }
        if (!known)
        {
            ignored.push_back("filter " + name);
            continue;
        }

        if (filter.kind == Kind::HueRotate)
        {
            filter.amount = 0;
            if (!args.empty())
            {
                size_t pos = 0;
                const auto number = parseNumberAt(args, pos);
                if (!number)
                {
                    ignored.push_back("filter " + name);
                    continue;
                }
                const auto unit = args.substr(pos);
                if (unit == "deg" || (unit.empty() && *number == 0.0))
                {
                    filter.amount = *number;
                }
                else if (unit == "rad")
                {
                    filter.amount = *number * 180.0 / 3.14159265358979323846;
                }
                else if (unit == "grad")
                {
                    filter.amount = *number * 0.9;
                }
                else if (unit == "turn")
                {
                    filter.amount = *number * 360.0;
                }
                else
                {
                    ignored.push_back("filter " + name);
                    continue;
                }
            }
        }
        else if (!args.empty())
        {
            size_t pos = 0;
            const auto number = parseNumberAt(args, pos);
            const auto unit = args.substr(pos);
            if (!number || (!unit.empty() && unit != "%"))
            {
                ignored.push_back("filter " + name);
                continue;
            }
            filter.amount = unit == "%" ? *number / 100.0 : *number;
        }
        out.push_back(filter);
    }
}

/// The `background-position` x value of a keyframe stop, if it has one.
/// `found` says whether the stop sets one at all, the result whether it could
/// be evaluated.
std::optional<double> keyframePositionX(
    const std::vector<KeyframeStop> &stops, double percent, const Props &props,
    double frame, double sheet, bool &found)
{
    found = false;
    string raw;
    for (const auto &stop : stops)
    {
        if (stop.percent != percent)
        {
            continue;
        }
        for (const auto &decl : stop.decls)
        {
            if (decl.name == "background-position")
            {
                const auto tokens = splitWhitespace(decl.value);
                if (!tokens.empty())
                {
                    raw = tokens[0];
                    found = true;
                }
            }
            else if (decl.name == "background-position-x")
            {
                raw = decl.value;
                found = true;
            }
        }
    }
    if (!found)
    {
        return std::nullopt;
    }
    return positionPx(raw, props, frame, sheet);
}

/// Evaluates the final property values of one emote.
namespace anim = dggcss::anim;

static_assert(static_cast<int>(anim::FilterKind::Brightness) ==
                  static_cast<int>(SpriteColorFilter::Kind::Brightness) &&
              static_cast<int>(anim::FilterKind::Contrast) ==
                  static_cast<int>(SpriteColorFilter::Kind::Contrast) &&
              static_cast<int>(anim::FilterKind::Saturate) ==
                  static_cast<int>(SpriteColorFilter::Kind::Saturate) &&
              static_cast<int>(anim::FilterKind::HueRotate) ==
                  static_cast<int>(SpriteColorFilter::Kind::HueRotate) &&
              static_cast<int>(anim::FilterKind::Grayscale) ==
                  static_cast<int>(SpriteColorFilter::Kind::Grayscale) &&
              static_cast<int>(anim::FilterKind::Sepia) ==
                  static_cast<int>(SpriteColorFilter::Kind::Sepia) &&
              static_cast<int>(anim::FilterKind::Invert) ==
                  static_cast<int>(SpriteColorFilter::Kind::Invert) &&
              static_cast<int>(anim::FilterKind::Opacity) ==
                  static_cast<int>(SpriteColorFilter::Kind::Opacity),
              "anim::FilterKind and SpriteColorFilter::Kind must match");

SpritePose poseOf(const anim::Computed &state, const anim::Box &box)
{
    SpritePose pose;
    const auto m = anim::matrixOf(state, box);
    pose.matrix = {m.a, m.b, m.c, m.d, m.e, m.f};
    pose.opacity = state.opacity;
    for (const auto &fn : state.filter)
    {
        pose.filters.push_back(SpriteColorFilter{
            .kind = static_cast<SpriteColorFilter::Kind>(fn.kind),
            .amount = fn.amount,
        });
    }
    pose.clip = anim::clipPx(state.clip, box);
    return pose;
}

/// Animations of `transform`, `opacity`, `filter` and `clip-path` can't run
/// in Chatterino, so they are baked: the keyframes are evaluated at a series
/// of moments, and every moment becomes one frame of the emote (a "pose" on
/// top of the frame the sheet shows). Only done if the emote has no sprite
/// animation of its own; the two aren't combined.
///
/// Leaves `style` alone (adding a note) if the animations can't be baked.
void bakeAnimations(const Props &props, const Keyframes &keyframes,
                    DggEmoteStyle &style, std::vector<string> &notes)
{
    const auto rawIt = props.find("animation-raw");
    if (rawIt == props.end() || style.sprite.offsets.size() != 1)
    {
        return;
    }
    const auto rawList = resolveVars(rawIt->second, props);
    if (!rawList || trimView(*rawList) == "none")
    {
        return;
    }
    const auto specs = anim::parseAnimationList(*rawList);
    if (!specs)
    {
        notes.push_back("animation (not understood): " + *rawList);
        return;
    }

    std::vector<anim::Animation> animations;
    for (const auto &spec : *specs)
    {
        if (spec.name.empty() || spec.name == "none")
        {
            continue;
        }
        const auto found = keyframes.find(spec.name);
        if (found == keyframes.end())
        {
            notes.push_back("animation " + spec.name + ": no @keyframes");
            continue;
        }

        std::vector<anim::Keyframe> frames;
        for (const auto &stop : found->second)
        {
            anim::Keyframe frame;
            frame.offset = stop.percent / 100.0;
            for (const auto &decl : stop.decls)
            {
                const auto value = resolveVars(decl.value, props);
                frame.decls.push_back(
                    anim::KeyframeDecl{decl.name, value ? *value : decl.value});
            }
            frames.push_back(std::move(frame));
        }

        string problem;
        auto built = anim::Animation::build(spec, frames, problem);
        if (!built)
        {
            // e.g. the `background-position` animation of a sprite sheet:
            // not this path's business
            if (problem.rfind("keyframe property background-position", 0) != 0)
            {
                notes.push_back("animation " + spec.name + ": " + problem);
            }
            continue;
        }
        animations.push_back(std::move(*built));
    }
    if (animations.empty())
    {
        return;
    }

    // What the element looks like when nothing is animated
    anim::Computed base;
    for (const char *name : {"transform", "translate", "rotate", "scale",
                             "opacity", "transform-origin", "clip-path"})
    {
        const auto it = props.find(name);
        if (it == props.end())
        {
            continue;
        }
        const auto resolved = resolveVars(it->second, props);
        if (!resolved)
        {
            continue;
        }
        if (auto parsed = anim::parseDeclaration(name, *resolved))
        {
            anim::setProp(base, parsed->first, std::move(parsed->second));
        }
    }
    for (const auto &filter : style.sprite.filters)
    {
        base.filter.push_back(anim::FilterFn{
            .kind = static_cast<anim::FilterKind>(filter.kind),
            .amount = filter.amount,
        });
    }

    const anim::Box box{
        .width = static_cast<double>(style.frameSize.width()),
        .height = static_cast<double>(style.frameSize.height()),
    };
    string problem;
    const auto baked =
        anim::bake(animations, base, box, anim::BakeOptions{}, problem);
    if (!baked)
    {
        notes.push_back("animation baking: " + problem);
        return;
    }
    // what the sprite path had to say about these animations is moot now
    std::erase_if(notes, [](const string &note) {
        return note.rfind("animation ", 0) == 0;
    });
    if (baked->truncated)
    {
        notes.emplace_back("animation baking: cut off after 10 s");
    }

    auto frames = baked->frames;
    if (frames.size() == 1)
    {
        // a limited animation that only has one distinct moment before it
        // rests: the second frame is what makes it count as an animation
        frames.push_back(frames.front());
    }

    const QPoint offset = style.sprite.offsets.front();
    style.sprite.offsets.assign(frames.size(), offset);
    style.sprite.durationsMs.clear();
    style.sprite.poses.clear();
    for (const auto &frame : frames)
    {
        style.sprite.durationsMs.push_back(frame.durationMs);
        style.sprite.poses.push_back(poseOf(frame.state, box));
    }
    // the poses carry the element's filters
    style.sprite.filters.clear();

    style.sprite.loopCount = baked->plays;
    style.sprite.restOffset = offset;
    style.sprite.restPose = poseOf(baked->rest, box);
}

/// `transform` without its `translate*()` functions: a shift would push part
/// of the frame out of its box, where it is cut off (chat-gui lets it
/// overflow instead).
string withoutTranslations(string_view transform)
{
    string out;
    size_t i = 0;
    while (i < transform.size())
    {
        if (transform.compare(i, 9, "translate") == 0)
        {
            const size_t open = transform.find('(', i);
            if (open != string_view::npos)
            {
                int depth = 0;
                size_t j = open;
                for (; j < transform.size(); j++)
                {
                    if (transform[j] == '(')
                    {
                        depth++;
                    }
                    else if (transform[j] == ')' && --depth == 0)
                    {
                        break;
                    }
                }
                i = j + 1;
                continue;
            }
        }
        out.push_back(transform[i]);
        i++;
    }
    return out;
}

/// An emote that steps through the frames of a sprite sheet can't have its
/// `transform` animated as well (see bakeAnimations()), but a plain
/// `transform` that stays the same, such as the `scale(-1, 1)` that makes one
/// of two neighbouring Chatting emotes face the other, can be drawn on every
/// frame.
void applyStaticTransform(const Props &props, DggEmoteStyle &style,
                          std::vector<string> &notes)
{
    if (style.sprite.hasPoses() || style.sprite.offsets.size() < 2)
    {
        return;
    }
    const auto it = props.find("transform");
    if (it == props.end())
    {
        return;
    }
    const auto resolved = resolveVars(it->second, props);
    if (!resolved)
    {
        return;
    }
    const string text = withoutTranslations(*resolved);
    if (trimView(text).empty() || trimView(text) == "none")
    {
        return;
    }

    anim::Computed base;
    if (auto parsed = anim::parseDeclaration("transform", text))
    {
        anim::setProp(base, parsed->first, std::move(parsed->second));
    }
    else
    {
        notes.push_back("transform (not understood): " + *resolved);
        return;
    }
    if (const auto origin = props.find("transform-origin");
        origin != props.end())
    {
        if (const auto value = resolveVars(origin->second, props))
        {
            if (auto parsed =
                    anim::parseDeclaration("transform-origin", *value))
            {
                anim::setProp(base, parsed->first, std::move(parsed->second));
            }
        }
    }
    for (const auto &filter : style.sprite.filters)
    {
        base.filter.push_back(anim::FilterFn{
            .kind = static_cast<anim::FilterKind>(filter.kind),
            .amount = filter.amount,
        });
    }

    const anim::Box box{
        .width = static_cast<double>(style.frameSize.width()),
        .height = static_cast<double>(style.frameSize.height()),
    };
    const SpritePose pose = poseOf(base, box);
    if (pose.isIdentity())
    {
        return;
    }

    style.sprite.poses.assign(style.sprite.offsets.size(), pose);
    style.sprite.restPose = pose;
    // the pose carries the element's filters
    style.sprite.filters.clear();
}

DggEmoteStyle computeStyle(const Props &props, const Keyframes &keyframes,
                           QSize sheetSize)
{
    DggEmoteStyle style;
    std::vector<string> notes;

    const auto get = [&](const char *name) -> const string * {
        const auto it = props.find(name);
        return it == props.end() ? nullptr : &it->second;
    };
    const auto length = [&](const char *name) -> std::optional<double> {
        const auto *raw = get(name);
        if (raw == nullptr)
        {
            return std::nullopt;
        }
        const auto resolved = resolveVars(*raw, props);
        if (!resolved)
        {
            return std::nullopt;
        }
        return parseLengthPx(*resolved);
    };

    const auto width = length("width");
    const auto height = length("height");
    if (!width || !height || *width < 1 || *height < 1)
    {
        return style;  // not valid: no size
    }

    const double sheetW = sheetSize.width();
    const double sheetH = sheetSize.height();

    // where the static image sits
    double x0 = 0;
    double y0 = 0;
    if (const auto *rawX = get("background-position-x"))
    {
        if (const auto x = positionPx(*rawX, props, *width, sheetW))
        {
            x0 = *x;
        }
        else
        {
            notes.emplace_back("background-position");
        }
    }
    if (const auto *rawY = get("background-position-y"))
    {
        if (const auto y = positionPx(*rawY, props, *height, sheetH))
        {
            y0 = *y;
        }
        else
        {
            notes.emplace_back("background-position");
        }
    }

    // The sprite animation, if any
    std::vector<double> xs;
    std::vector<int> durations;
    // where the last keyframe leaves the background (`animation-fill-mode:
    // forwards` keeps it there)
    double endX = x0;

    const auto *nameValue = get("animation-name");
    if (nameValue != nullptr && *nameValue != "none")
    {
        const string &name = *nameValue;
        const auto found = keyframes.find(name);

        const auto *rawDuration = get("animation-duration");
        const auto duration = rawDuration != nullptr
                                  ? evalDurationMs(*rawDuration, props)
                                  : std::nullopt;
        const auto *rawTiming = get("animation-timing-function");
        const auto timing = rawTiming != nullptr
                                ? resolveVars(*rawTiming, props)
                                : std::nullopt;
        const auto steps = timing ? parseSteps(*timing) : std::nullopt;

        if (found == keyframes.end())
        {
            notes.push_back("animation " + name + ": no @keyframes");
        }
        else if (!steps)
        {
            notes.push_back("animation " + name + ": timing " +
                            (timing ? *timing : string("?")));
        }
        else if (!duration || *duration < 1)
        {
            notes.push_back("animation " + name + ": duration");
        }
        else if (steps->count > MAX_STEPS)
        {
            notes.push_back("animation " + name + ": too many steps");
        }
        else if (std::any_of(found->second.begin(), found->second.end(),
                             [](const KeyframeStop &stop) {
                                 return stop.percent > 0 && stop.percent < 100;
                             }))
        {
            notes.push_back("animation " + name + ": keyframes in between");
        }
        else
        {
            bool hasFrom = false;
            bool hasTo = false;
            const auto from = keyframePositionX(found->second, 0, props,
                                                *width, sheetW, hasFrom);
            const auto to = keyframePositionX(found->second, 100, props,
                                              *width, sheetW, hasTo);

            // a missing stop is the element's own value
            const double a = hasFrom ? from.value_or(x0) : x0;
            const double b = hasTo ? to.value_or(x0) : x0;
            if ((hasFrom && !from) || (hasTo && !to))
            {
                notes.push_back("animation " + name + ": keyframe position");
            }
            else if (a == b)
            {
                notes.push_back("animation " + name + ": doesn't move");
            }
            else
            {
                const int n = steps->count;
                const double step = (b - a) / n;
                for (int i = 0; i < n; i++)
                {
                    xs.push_back(a + step * (steps->jumpStart ? i + 1 : i));
                }

                const auto *direction = get("animation-direction");
                endX = b;
                if (direction != nullptr && *direction == "reverse")
                {
                    std::reverse(xs.begin(), xs.end());
                    endX = a;
                }
                else if (direction != nullptr && *direction != "normal")
                {
                    notes.push_back("animation-direction " + *direction);
                }

                // spread the milliseconds over the frames without losing any
                const auto total = static_cast<int>(std::lround(*duration));
                const int base = total / n;
                const int extra = total % n;
                for (int i = 0; i < n; i++)
                {
                    durations.push_back(base + (i < extra ? 1 : 0));
                }
            }
        }
    }

    if (xs.empty())
    {
        xs.push_back(x0);
        durations.push_back(0);
    }

    style.valid = true;
    style.frameSize = QSize(static_cast<int>(std::lround(*width)),
                            static_cast<int>(std::lround(*height)));
    style.sprite.frameSize = style.frameSize;
    for (const double x : xs)
    {
        // a background moved left shows a part of the image further right
        style.sprite.offsets.emplace_back(static_cast<int>(std::lround(-x)),
                                          static_cast<int>(std::lround(-y0)));
    }
    style.sprite.durationsMs = std::move(durations);

    // How often it plays, and what it shows afterwards. The CSS default is
    // one play; `infinite` is the only way to loop.
    if (style.sprite.offsets.size() > 1)
    {
        const auto first = [&](const char *name) -> std::optional<string> {
            const auto *raw = get(name);
            if (raw == nullptr)
            {
                return std::nullopt;
            }
            const auto resolved = resolveVars(*raw, props);
            if (!resolved)
            {
                return std::nullopt;
            }
            const auto parts = splitTopLevel(*resolved, ',');
            if (parts.empty())
            {
                return std::nullopt;
            }
            return toLower(trimView(parts.front()));
        };

        int loops = 1;
        if (const auto count = first("animation-iteration-count"))
        {
            if (*count == "infinite")
            {
                loops = 0;
            }
            else if (const auto n = parseNumber(*count); n && *n > 0)
            {
                // a part of a play (2.5) is rounded up
                loops = static_cast<int>(std::min(std::ceil(*n), 1e6));
            }
        }

        if (loops > 0)
        {
            const auto fill = first("animation-fill-mode");
            const bool keepEnd = fill && (*fill == "forwards" || *fill == "both");
            const double restX = keepEnd ? endX : x0;

            style.sprite.loopCount = loops;
            style.sprite.restOffset =
                QPoint(static_cast<int>(std::lround(-restX)),
                       static_cast<int>(std::lround(-y0)));
        }
        if (const auto delay = first("animation-delay");
            delay && *delay != "0s" && *delay != "0ms" && *delay != "0")
        {
            notes.emplace_back("animation-delay " + *delay);
        }
    }

    if (const auto *filter = get("filter"))
    {
        parseFilters(*filter, props, style.sprite.filters, notes);
    }

    // `background-size: 32px 32px` (or one value for both) shrinks or
    // stretches the whole image, e.g. a 500px GIF drawn at 32px (BlueWave).
    if (const auto *raw = get("background-size"))
    {
        if (const auto resolved = resolveVars(*raw, props))
        {
            const string_view text = trimView(*resolved);
            const auto space = text.find(' ');
            const auto first = parseLengthPx(text.substr(0, space));
            const auto second = space == string_view::npos
                                    ? first
                                    : parseLengthPx(text.substr(space + 1));
            if (first && second && *first >= 1 && *second >= 1)
            {
                style.backgroundSize =
                    QSize(static_cast<int>(std::lround(*first)),
                          static_cast<int>(std::lround(*second)));
            }
            else if (text != "auto" && text != "initial")
            {
                notes.emplace_back("background-size " + string(text));
            }
        }
    }
    bakeAnimations(props, keyframes, style, notes);
    applyStaticTransform(props, style, notes);

    // A `transform` is only reproduced where an animation moves it or, for a
    // sprite animation, where it stays the same (without its translation)
    std::vector<const char *> ignoredProps{"mask-image", "mix-blend-mode"};
    if (!style.sprite.hasPoses())
    {
        ignoredProps.push_back("transform");
    }
    for (const char *ignored : ignoredProps)
    {
        if (const auto *value = get(ignored); value != nullptr &&
                                              *value != "none")
        {
            notes.emplace_back(ignored);
        }
    }

    for (const auto &note : notes)
    {
        style.notes.append(QString::fromStdString(note));
    }
    return style;
}

}  // namespace

// ---------------------------------------------------------------------------
// DggEmoteStyle
// ---------------------------------------------------------------------------

bool DggEmoteStyle::isWholeImage(QSize sheetSize) const
{
    return this->valid && !sheetSize.isEmpty() && !this->sprite.hasPoses() &&
           this->sprite.offsets.size() == 1 &&
           this->sprite.offsets[0] == QPoint(0, 0) &&
           this->frameSize == sheetSize && this->sprite.filters.empty();
}

QString DggEmoteStyle::cacheKey() const
{
    if (!this->valid)
    {
        return QStringLiteral("plain");
    }
    return this->sprite.cacheKey() +
           QStringLiteral("|bg%1x%2").arg(this->backgroundSize.width()).arg(
               this->backgroundSize.height());
}

// ---------------------------------------------------------------------------
// DggEmoteCss
// ---------------------------------------------------------------------------

DggEmoteCss DggEmoteCss::parse(const QString &css)
{
    DggEmoteCss out;
    const string text = stripComments(css.toStdString());
    int order = 0;

    forEachBlock(text, [&](string_view prelude, string_view body) {
        if (startsWith(prelude, "@keyframes") ||
            startsWith(prelude, "@-webkit-keyframes"))
        {
            const auto fields = splitWhitespace(prelude);
            if (fields.size() < 2)
            {
                return;
            }

            std::vector<KeyframeStop> stops;
            forEachBlock(body, [&](string_view selector, string_view stopBody) {
                const auto decls = parseDeclarations(stopBody);
                for (const auto &part : splitTopLevel(selector, ','))
                {
                    const auto key = toLower(trimView(part));
                    double percent = 0;
                    if (key == "from")
                    {
                        percent = 0;
                    }
                    else if (key == "to")
                    {
                        percent = 100;
                    }
                    else if (!key.empty() && key.back() == '%')
                    {
                        const auto number = parseNumber(
                            string_view(key).substr(0, key.size() - 1));
                        if (!number)
                        {
                            continue;
                        }
                        percent = *number;
                    }
                    else
                    {
                        continue;
                    }
                    stops.push_back(KeyframeStop{percent, decls});
                }
            });
            out.keyframes_[fields[1]] = std::move(stops);
            return;
        }

        if (startsWith(prelude, "@"))
        {
            // @supports, @media, ... aren't looked into
            out.unsupported_["at-rule"]++;
            out.stats_.unsupportedRules++;
            return;
        }

        const auto decls = parseDeclarations(body);
        for (const auto &selector : splitTopLevel(prelude, ','))
        {
            auto analysis = analyseSelector(trimView(selector));
            if (!analysis.rejected.empty())
            {
                out.unsupported_[analysis.rejected]++;
                out.stats_.unsupportedRules++;
                continue;
            }
            if (analysis.hover)
            {
                out.stats_.hoverRules++;
                continue;
            }

            Rule rule;
            rule.parts = std::move(analysis.parts);
            rule.specificity = analysis.specificity;
            rule.order = ++order;
            rule.dependsOnContext = analysis.dependsOnContext;
            rule.decls = decls;
            out.rules_[analysis.emote].push_back(std::move(rule));
            out.stats_.rules++;
        }
    });

    out.stats_.keyframes = out.keyframes_.size();
    out.stats_.emotes = out.rules_.size();
    return out;
}

QStringList DggEmoteCss::describeUnsupported() const
{
    std::vector<std::pair<std::string, size_t>> sorted(
        this->unsupported_.begin(), this->unsupported_.end());
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto &a, const auto &b) {
                         return a.second > b.second;
                     });

    QStringList out;
    for (const auto &[reason, count] : sorted)
    {
        out.append(QString::fromStdString(reason) + QStringLiteral(": ") +
                   QString::number(static_cast<qulonglong>(count)));
    }
    return out;
}

bool DggEmoteCss::hasEmote(const QString &emote) const
{
    return this->rules_.contains(emote.toStdString());
}

bool DggEmoteCss::dependsOnContext(const QString &emote) const
{
    const auto it = this->rules_.find(emote.toStdString());
    if (it == this->rules_.end())
    {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(),
                       [](const Rule &rule) {
                           return rule.dependsOnContext;
                       });
}

DggEmoteStyle DggEmoteCss::resolve(const QString &emote,
                                   const DggEmoteContext &context,
                                   QSize sheetSize) const
{
    const string name = emote.toStdString();
    const auto it = this->rules_.find(name);
    if (it == this->rules_.end())
    {
        return {};
    }

    const Dom dom = buildDom(name, context);
    std::vector<const Rule *> matched;
    for (const auto &rule : it->second)
    {
        if (matchChain(rule.parts, rule.parts.size() - 1, dom, dom.emote))
        {
            matched.push_back(&rule);
        }
    }
    if (matched.empty())
    {
        return {};
    }

    // The cascade: higher specificity wins, later rules win ties, and
    // `!important` beats both.
    std::stable_sort(matched.begin(), matched.end(),
                     [](const Rule *a, const Rule *b) {
                         if (a->specificity != b->specificity)
                         {
                             return a->specificity < b->specificity;
                         }
                         return a->order < b->order;
                     });

    Props props;
    for (const bool important : {false, true})
    {
        for (const auto *rule : matched)
        {
            for (const auto &decl : rule->decls)
            {
                if (decl.important == important)
                {
                    setProperty(props, decl.name, decl.value);
                }
            }
        }
    }

    return computeStyle(props, this->keyframes_, sheetSize);
}

}  // namespace chatterino
