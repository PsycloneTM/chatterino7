#include "providers/dgg/DggEmoteCss.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>

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

    // The subject: exactly `.emote.NAME`
    const auto &subject = out.parts.back().compound;
    if (subject.classes.size() != 2 || !subject.attrs.empty() ||
        !contains(subject.classes, "emote"))
    {
        out.rejected = "selector isn't for a single emote";
        return out;
    }
    out.emote = subject.classes[0] == "emote" ? subject.classes[1]
                                              : subject.classes[0];
    if (out.emote == "emote")
    {
        out.rejected = "selector isn't for a single emote";
        return out;
    }

    for (const auto &part : out.parts)
    {
        for (const auto &pseudo : part.compound.pseudos)
        {
            if (pseudo == ":hover")
            {
                out.hover = true;
            }
            else if (startsWith(pseudo, "::") || pseudo == ":before" ||
                     pseudo == ":after")
            {
                out.rejected = "pseudo-element";
                return out;
            }
            else
            {
                const auto paren = pseudo.find('(');
                out.rejected =
                    "pseudo-class " +
                    (paren == NPOS ? pseudo : pseudo.substr(0, paren));
                return out;
            }
        }
    }

    // Everything above the subject must be a part of chat-gui's message DOM
    // that we know: the message, the nick, or the text.
    for (size_t i = 0; i + 1 < out.parts.size(); i++)
    {
        const auto &c = out.parts[i].compound;
        const bool isMessage =
            contains(c.classes, "msg-chat") ||
            (c.classes.empty() && !c.attrs.empty());
        const bool isUser = contains(c.classes, "user");
        const bool isText = c.classes.size() == 1 && c.classes[0] == "text";

        if (isMessage && !isUser && !isText)
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
            if (contains(c.classes, "emote") || contains(c.classes, "text"))
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
        else if (isUser && !isMessage && c.attrs.empty())
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
        else
        {
            out.rejected = "selector context";
            return out;
        }
    }

    for (size_t i = 1; i < out.parts.size(); i++)
    {
        const char comb = out.parts[i].combinator;
        if (comb == '+')
        {
            out.rejected = "adjacent sibling";
            return out;
        }
        if (comb == '~' && i + 1 == out.parts.size())
        {
            out.rejected = "sibling emote";
            return out;
        }
    }

    for (const auto &part : out.parts)
    {
        out.specificity += static_cast<int>(part.compound.classes.size() +
                                            part.compound.attrs.size() +
                                            part.compound.pseudos.size());
    }
    return out;
}

// ---------------------------------------------------------------------------
// Matching against a chat-gui message
// ---------------------------------------------------------------------------

/// The four elements of a message that selectors can talk about:
/// message (M) > [nick (U) ~ text (T) > emote (E)].
struct Node {
    char kind = 0;
    std::vector<string> classes;
    string username;
    int parent = -1;
    std::vector<int> previousSiblings;
};

struct Dom {
    std::vector<Node> nodes;  // M, U, T, E
    int emote = 3;
};

Dom buildDom(const string &emote, const DggEmoteContext &ctx)
{
    Dom dom;
    dom.nodes.resize(4);

    Node &m = dom.nodes[0];
    m.kind = 'M';
    m.classes = {"msg-chat", "msg-user"};
    for (const auto &cls : ctx.messageClasses)
    {
        m.classes.push_back(cls.toStdString());
    }
    m.username = ctx.username.toStdString();

    Node &u = dom.nodes[1];
    u.kind = 'U';
    u.classes = {"user"};
    if (!ctx.nickColorFlair.isEmpty())
    {
        u.classes.push_back(ctx.nickColorFlair.toStdString());
    }
    u.parent = 0;

    Node &t = dom.nodes[2];
    t.kind = 'T';
    t.classes = {"text"};
    t.parent = 0;
    t.previousSiblings = {1};

    Node &e = dom.nodes[3];
    e.kind = 'E';
    e.classes = {"emote", emote};
    e.parent = 2;

    return dom;
}

bool isSubset(const std::vector<string> &wanted, const std::vector<string> &have)
{
    return std::all_of(wanted.begin(), wanted.end(),
                       [&](const string &cls) { return contains(have, cls); });
}

bool compoundMatches(const Compound &c, const Node &node)
{
    switch (node.kind)
    {
        case 'M': {
            if (c.classes.empty() && c.attrs.empty())
            {
                return false;
            }
            if (contains(c.classes, "user") || contains(c.classes, "text") ||
                contains(c.classes, "emote"))
            {
                return false;
            }
            if (!isSubset(c.classes, node.classes))
            {
                return false;
            }
            for (const auto &attr : c.attrs)
            {
                if (attr.name != "data-username" || attr.op != "=" ||
                    attr.value != node.username)
                {
                    return false;
                }
            }
            return true;
        }
        case 'U':
            return contains(c.classes, "user") && c.attrs.empty() &&
                   isSubset(c.classes, node.classes);
        case 'T':
            return c.classes.size() == 1 && c.classes[0] == "text" &&
                   c.attrs.empty();
        case 'E':
            return c.attrs.empty() && isSubset(c.classes, node.classes);
        default:
            return false;
    }
}

bool matchChain(const std::vector<SelectorPart> &parts, size_t index,
                const Dom &dom, int nodeIndex)
{
    const Node &node = dom.nodes[static_cast<size_t>(nodeIndex)];
    if (!compoundMatches(parts[index].compound, node))
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
        case '~':
            return std::any_of(node.previousSiblings.begin(),
                               node.previousSiblings.end(), [&](int s) {
                                   return matchChain(parts, index - 1, dom, s);
                               });
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
        return;
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
    for (const char *ignored : {"transform", "mask-image", "mix-blend-mode"})
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
    return this->valid && !sheetSize.isEmpty() &&
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
    return this->sprite.cacheKey();
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
