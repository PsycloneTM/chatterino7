#include "providers/dgg/DggCssAnimation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace chatterino::dggcss::anim {

namespace {

using std::string;
using std::string_view;

constexpr double PI = 3.14159265358979323846;
constexpr double INF = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// Strings and numbers
// ---------------------------------------------------------------------------

bool isSpace(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

string_view trim(string_view s)
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

string lower(string_view s)
{
    string out(s);
    for (auto &c : out)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool startsWith(string_view s, string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

/// Splits at `sep` outside of parentheses.
std::vector<string_view> splitTop(string_view s, char sep)
{
    std::vector<string_view> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
        const char c = s[i];
        if (c == '(')
        {
            depth++;
        }
        else if (c == ')')
        {
            depth--;
        }
        else if (c == sep && depth == 0)
        {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

/// Splits at whitespace outside of parentheses.
std::vector<string_view> splitSpace(string_view s)
{
    std::vector<string_view> out;
    int depth = 0;
    size_t start = string_view::npos;
    for (size_t i = 0; i < s.size(); i++)
    {
        const char c = s[i];
        if (c == '(')
        {
            depth++;
        }
        else if (c == ')')
        {
            depth--;
        }
        if (isSpace(c) && depth == 0)
        {
            if (start != string_view::npos)
            {
                out.push_back(s.substr(start, i - start));
                start = string_view::npos;
            }
        }
        else if (start == string_view::npos)
        {
            start = i;
        }
    }
    if (start != string_view::npos)
    {
        out.push_back(s.substr(start));
    }
    return out;
}

/// The whole of `s` as a number, nothing else.
std::optional<double> parseNumber(string_view s)
{
    s = trim(s);
    if (s.empty())
    {
        return std::nullopt;
    }
    const string text(s);
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    // strtod also takes "inf", "nan" and hex floats. CSS doesn't.
    for (const char c : text)
    {
        if (std::isalpha(static_cast<unsigned char>(c)) && c != 'e' &&
            c != 'E')
        {
            return std::nullopt;
        }
    }
    return value;
}

/// `number` + `unit` (lower case, possibly empty).
std::optional<std::pair<double, string>> parseDimension(string_view s)
{
    s = trim(s);
    size_t split = s.size();
    while (split > 0)
    {
        const char c = s[split - 1];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '%')
        {
            split--;
        }
        else
        {
            break;
        }
    }
    const string unit = lower(s.substr(split));
    const string_view numberPart = s.substr(0, split);
    const auto number = parseNumber(numberPart);
    if (!number)
    {
        return std::nullopt;
    }
    return std::make_pair(*number, unit);
}

std::optional<double> parseTimeMs(string_view s)
{
    const auto dim = parseDimension(s);
    if (!dim)
    {
        return std::nullopt;
    }
    if (dim->second == "ms")
    {
        return dim->first;
    }
    if (dim->second == "s")
    {
        return dim->first * 1000.0;
    }
    return std::nullopt;
}

/// An angle in degrees. A bare `0` is allowed, too.
std::optional<double> parseAngleDeg(string_view s)
{
    const auto dim = parseDimension(s);
    if (!dim)
    {
        return std::nullopt;
    }
    const auto &[v, unit] = *dim;
    if (unit.empty())
    {
        return v == 0 ? std::optional<double>(0.0) : std::nullopt;
    }
    if (unit == "deg")
    {
        return v;
    }
    if (unit == "rad")
    {
        return v * 180.0 / PI;
    }
    if (unit == "grad")
    {
        return v * 0.9;
    }
    if (unit == "turn")
    {
        return v * 360.0;
    }
    return std::nullopt;
}

std::optional<Length> parseLength(string_view s)
{
    const auto dim = parseDimension(s);
    if (!dim)
    {
        return std::nullopt;
    }
    const auto &[v, unit] = *dim;
    if (unit == "px")
    {
        return Length{v, false};
    }
    if (unit == "%")
    {
        return Length{v, true};
    }
    if (unit.empty() && v == 0)
    {
        return Length{0, false};
    }
    return std::nullopt;
}

double clamp(double v, double lo, double hi)
{
    return std::min(hi, std::max(lo, v));
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

double toPx(const Length &l, double reference)
{
    return l.percent ? l.value * reference / 100.0 : l.value;
}

// ---------------------------------------------------------------------------
// Parsing the property values
// ---------------------------------------------------------------------------

/// The text inside the parentheses of `name(...)`, if `call` is that.
std::optional<string_view> callArgs(string_view call, string_view name)
{
    call = trim(call);
    if (call.size() <= name.size() + 1 || call.back() != ')')
    {
        return std::nullopt;
    }
    if (lower(call.substr(0, name.size())) != name ||
        call[name.size()] != '(')
    {
        return std::nullopt;
    }
    return call.substr(name.size() + 1, call.size() - name.size() - 2);
}

/// `auto`, or a length in px or % (a bare 0 too).
std::optional<LenOrAuto> parseLenOrAuto(string_view text)
{
    text = trim(text);
    if (lower(text) == "auto")
    {
        return LenOrAuto{};
    }
    if (const auto length = parseLength(text))
    {
        return LenOrAuto{false, *length};
    }
    return std::nullopt;
}

/// `normal`/`initial`, a length, a percentage or a bare number (a multiple of
/// the font size, kept as a percentage).
std::optional<LenOrAuto> parseLineHeight(string_view text)
{
    text = trim(text);
    const string l = lower(text);
    if (l == "normal" || l == "initial")
    {
        return LenOrAuto{};
    }
    if (const auto length = parseLength(text))
    {
        return LenOrAuto{false, *length};
    }
    if (const auto number = parseNumber(text); number && *number >= 0)
    {
        return LenOrAuto{false, Length{*number * 100.0, true}};
    }
    return std::nullopt;
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/// `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()` (comma
/// separated), `transparent`, `black` and `white`.
std::optional<std::array<double, 4>> parseColor(string_view text)
{
    text = trim(text);
    const string l = lower(text);
    if (l == "transparent")
    {
        return std::array<double, 4>{0, 0, 0, 0};
    }
    if (l == "black")
    {
        return std::array<double, 4>{0, 0, 0, 1};
    }
    if (l == "white")
    {
        return std::array<double, 4>{255, 255, 255, 1};
    }
    if (!text.empty() && text.front() == '#')
    {
        const auto digits = text.substr(1);
        std::array<int, 8> v{};
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i >= v.size() || hexDigit(digits[i]) < 0)
            {
                return std::nullopt;
            }
            v[i] = hexDigit(digits[i]);
        }
        if (digits.size() == 3 || digits.size() == 4)
        {
            return std::array<double, 4>{
                static_cast<double>(v[0] * 17), static_cast<double>(v[1] * 17),
                static_cast<double>(v[2] * 17),
                digits.size() == 4 ? v[3] * 17 / 255.0 : 1.0};
        }
        if (digits.size() == 6 || digits.size() == 8)
        {
            return std::array<double, 4>{
                static_cast<double>(v[0] * 16 + v[1]),
                static_cast<double>(v[2] * 16 + v[3]),
                static_cast<double>(v[4] * 16 + v[5]),
                digits.size() == 8 ? (v[6] * 16 + v[7]) / 255.0 : 1.0};
        }
        return std::nullopt;
    }
    for (const char *name : {"rgba", "rgb"})
    {
        if (const auto args = callArgs(text, name))
        {
            const auto parts = splitTop(*args, ',');
            if (parts.size() != 3 && parts.size() != 4)
            {
                return std::nullopt;
            }
            std::array<double, 4> out{0, 0, 0, 1};
            for (size_t i = 0; i < 3; i++)
            {
                const auto n = parseNumber(parts[i]);
                if (!n)
                {
                    return std::nullopt;
                }
                out[i] = clamp(*n, 0.0, 255.0);
            }
            if (parts.size() == 4)
            {
                const auto a = parseNumber(parts[3]);
                if (!a)
                {
                    return std::nullopt;
                }
                out[3] = clamp(*a, 0.0, 1.0);
            }
            return out;
        }
    }
    return std::nullopt;
}

/// `none`, or a comma separated list of
/// `[inset] <x> <y> [<blur> [<spread>]] [<color>]`.
std::optional<ShadowList> parseShadowList(string_view text)
{
    text = trim(text);
    if (lower(text) == "none")
    {
        return ShadowList{};
    }
    ShadowList out;
    for (const auto entry : splitTop(text, ','))
    {
        Shadow shadow;
        std::vector<double> numbers;
        bool haveColor = false;
        for (const auto token : splitSpace(entry))
        {
            if (lower(token) == "inset")
            {
                shadow.inset = true;
            }
            else if (const auto length = parseLength(token))
            {
                if (length->percent)
                {
                    return std::nullopt;
                }
                numbers.push_back(length->value);
            }
            else if (const auto color = parseColor(token))
            {
                if (haveColor)
                {
                    return std::nullopt;
                }
                shadow.rgba = *color;
                haveColor = true;
            }
            else
            {
                return std::nullopt;
            }
        }
        if (numbers.size() < 2 || numbers.size() > 4)
        {
            return std::nullopt;
        }
        shadow.x = numbers[0];
        shadow.y = numbers[1];
        shadow.blur = numbers.size() > 2 ? std::max(0.0, numbers[2]) : 0.0;
        shadow.spread = numbers.size() > 3 ? numbers[3] : 0.0;
        out.push_back(shadow);
    }
    return out;
}

void appendUtf8(string &out, unsigned long cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// `none`, `normal`, or one quoted string (with CSS escapes). The text is
/// UTF-8, like the stylesheet.
std::optional<Content> parseContent(string_view text)
{
    text = trim(text);
    const string l = lower(text);
    if (l == "none" || l == "normal")
    {
        return Content{};
    }
    if (text.size() < 2 || (text.front() != '"' && text.front() != '\'') ||
        text.back() != text.front())
    {
        return std::nullopt;
    }
    const char quote = text.front();
    const auto inner = text.substr(1, text.size() - 2);
    Content out;
    out.none = false;
    for (size_t i = 0; i < inner.size(); i++)
    {
        const char c = inner[i];
        if (c == quote)
        {
            return std::nullopt;  // several strings
        }
        if (c != '\\')
        {
            out.text.push_back(c);
            continue;
        }
        i++;
        if (i >= inner.size())
        {
            return std::nullopt;
        }
        if (hexDigit(inner[i]) >= 0)
        {
            unsigned long cp = 0;
            size_t digits = 0;
            while (i < inner.size() && digits < 6 && hexDigit(inner[i]) >= 0)
            {
                cp = cp * 16 + static_cast<unsigned long>(hexDigit(inner[i]));
                i++;
                digits++;
            }
            if (i < inner.size() && isSpace(inner[i]))
            {
                i++;
            }
            i--;
            appendUtf8(out.text, cp);
        }
        else if (inner[i] == '\n')
        {
            // a line continuation
        }
        else
        {
            out.text.push_back(inner[i]);
        }
    }
    return out;
}

std::optional<TransformList> parseTransformList(string_view text)
{
    text = trim(text);
    if (lower(text) == "none")
    {
        return TransformList{};
    }

    TransformList out;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && isSpace(text[i]))
        {
            i++;
        }
        if (i >= text.size())
        {
            break;
        }

        const auto open = text.find('(', i);
        if (open == string_view::npos)
        {
            return std::nullopt;
        }
        int depth = 0;
        size_t close = open;
        for (; close < text.size(); close++)
        {
            if (text[close] == '(')
            {
                depth++;
            }
            else if (text[close] == ')' && --depth == 0)
            {
                break;
            }
        }
        if (close >= text.size())
        {
            return std::nullopt;
        }

        const string name = lower(trim(text.substr(i, open - i)));
        const auto args = splitTop(text.substr(open + 1, close - open - 1), ',');
        i = close + 1;

        TransformFn fn;
        const auto length = [&](size_t index) -> std::optional<Length> {
            return index < args.size() ? parseLength(args[index])
                                       : std::optional<Length>(Length{});
        };
        const auto number = [&](size_t index) -> std::optional<double> {
            return index < args.size() ? parseNumber(args[index])
                                       : std::nullopt;
        };

        if (name == "translate" || name == "translatex" ||
            name == "translatey" || name == "translate3d")
        {
            fn.kind = TransformFn::Kind::Translate;
            if (name == "translatey")
            {
                const auto y = length(0);
                if (args.size() != 1 || !y)
                {
                    return std::nullopt;
                }
                fn.y = *y;
            }
            else
            {
                const auto x = length(0);
                const auto y = length(1);
                const size_t expected = name == "translate3d" ? 3 : 2;
                if (args.empty() || args.size() > expected || !x || !y ||
                    (name == "translatex" && args.size() != 1) ||
                    (name == "translate3d" && args.size() != 3))
                {
                    return std::nullopt;
                }
                if (name == "translate3d")
                {
                    const auto z = length(2);
                    if (!z || z->value != 0)
                    {
                        return std::nullopt;
                    }
                }
                fn.x = *x;
                fn.y = *y;
            }
        }
        else if (name == "rotate" || name == "rotatez" || name == "rotate3d")
        {
            fn.kind = TransformFn::Kind::Rotate;
            double sign = 1;
            std::optional<double> angle;
            if (name == "rotate3d")
            {
                // only a turn around the z axis is a flat rotation
                const auto x = number(0);
                const auto y = number(1);
                const auto z = number(2);
                if (args.size() != 4 || !x || !y || !z || *x != 0 || *y != 0 ||
                    *z == 0)
                {
                    return std::nullopt;
                }
                sign = *z > 0 ? 1 : -1;
                angle = parseAngleDeg(args[3]);
            }
            else
            {
                angle = args.size() == 1 ? parseAngleDeg(args[0]) : std::nullopt;
            }
            if (!angle)
            {
                return std::nullopt;
            }
            fn.n[0] = sign * *angle;
        }
        else if (name == "scale" || name == "scalex" || name == "scaley" ||
                 name == "scale3d")
        {
            fn.kind = TransformFn::Kind::Scale;
            fn.n[0] = 1;
            fn.n[1] = 1;
            if (name == "scalex" || name == "scaley")
            {
                const auto v = args.size() == 1 ? number(0) : std::nullopt;
                if (!v)
                {
                    return std::nullopt;
                }
                fn.n[name == "scalex" ? 0 : 1] = *v;
            }
            else if (name == "scale3d")
            {
                const auto x = number(0);
                const auto y = number(1);
                const auto z = number(2);
                if (args.size() != 3 || !x || !y || !z || *z != 1)
                {
                    return std::nullopt;
                }
                fn.n[0] = *x;
                fn.n[1] = *y;
            }
            else
            {
                const auto x = number(0);
                const auto y = args.size() > 1 ? number(1) : x;
                if (args.empty() || args.size() > 2 || !x || !y)
                {
                    return std::nullopt;
                }
                fn.n[0] = *x;
                fn.n[1] = *y;
            }
        }
        else if (name == "skew" || name == "skewx" || name == "skewy")
        {
            fn.kind = TransformFn::Kind::Skew;
            if (name == "skew")
            {
                const auto x = args.empty() ? std::nullopt : parseAngleDeg(args[0]);
                const auto y = args.size() > 1 ? parseAngleDeg(args[1])
                                               : std::optional<double>(0.0);
                if (args.empty() || args.size() > 2 || !x || !y)
                {
                    return std::nullopt;
                }
                fn.n[0] = *x;
                fn.n[1] = *y;
            }
            else
            {
                const auto v = args.size() == 1 ? parseAngleDeg(args[0])
                                                : std::nullopt;
                if (!v)
                {
                    return std::nullopt;
                }
                fn.n[name == "skewx" ? 0 : 1] = *v;
            }
        }
        else if (name == "matrix")
        {
            fn.kind = TransformFn::Kind::Matrix;
            if (args.size() != 6)
            {
                return std::nullopt;
            }
            for (size_t k = 0; k < 6; k++)
            {
                const auto v = parseNumber(args[k]);
                if (!v)
                {
                    return std::nullopt;
                }
                fn.n[k] = *v;
            }
        }
        else
        {
            return std::nullopt;
        }
        out.push_back(fn);
    }
    return out;
}

/// `translate: 10px 20px`, `rotate: 45deg`, `scale: 1.5 2` (space separated)
std::optional<TransformList> parseIndividual(Prop prop, string_view text)
{
    text = trim(text);
    if (lower(text) == "none")
    {
        return TransformList{};
    }
    const auto parts = splitSpace(text);
    TransformFn fn;
    if (prop == Prop::Translate)
    {
        fn.kind = TransformFn::Kind::Translate;
        if (parts.empty() || parts.size() > 2)
        {
            return std::nullopt;
        }
        const auto x = parseLength(parts[0]);
        const auto y = parts.size() > 1 ? parseLength(parts[1])
                                        : std::optional<Length>(Length{});
        if (!x || !y)
        {
            return std::nullopt;
        }
        fn.x = *x;
        fn.y = *y;
    }
    else if (prop == Prop::Rotate)
    {
        fn.kind = TransformFn::Kind::Rotate;
        const auto a = parts.size() == 1 ? parseAngleDeg(parts[0])
                                         : std::nullopt;
        if (!a)
        {
            return std::nullopt;
        }
        fn.n[0] = *a;
    }
    else
    {
        fn.kind = TransformFn::Kind::Scale;
        if (parts.empty() || parts.size() > 2)
        {
            return std::nullopt;
        }
        const auto factor = [](string_view s) -> std::optional<double> {
            if (!s.empty() && s.back() == '%')
            {
                const auto v = parseNumber(s.substr(0, s.size() - 1));
                return v ? std::optional<double>(*v / 100.0) : std::nullopt;
            }
            return parseNumber(s);
        };
        const auto x = factor(parts[0]);
        const auto y = parts.size() > 1 ? factor(parts[1]) : x;
        if (!x || !y)
        {
            return std::nullopt;
        }
        fn.n[0] = *x;
        fn.n[1] = *y;
    }
    return TransformList{fn};
}

std::optional<FilterList> parseFilterList(string_view text)
{
    text = trim(text);
    if (lower(text) == "none")
    {
        return FilterList{};
    }

    FilterList out;
    for (const auto part : splitSpace(text))
    {
        const auto open = part.find('(');
        if (open == string_view::npos || part.back() != ')')
        {
            return std::nullopt;
        }
        const string name = lower(trim(part.substr(0, open)));
        const auto arg = trim(part.substr(open + 1, part.size() - open - 2));

        FilterFn fn;
        if (name == "hue-rotate")
        {
            fn.kind = FilterKind::HueRotate;
            const auto angle = arg.empty() ? std::optional<double>(0.0)
                                           : parseAngleDeg(arg);
            if (!angle)
            {
                return std::nullopt;
            }
            fn.amount = *angle;
            out.push_back(fn);
            continue;
        }

        if (name == "brightness")
        {
            fn.kind = FilterKind::Brightness;
        }
        else if (name == "contrast")
        {
            fn.kind = FilterKind::Contrast;
        }
        else if (name == "saturate")
        {
            fn.kind = FilterKind::Saturate;
        }
        else if (name == "grayscale")
        {
            fn.kind = FilterKind::Grayscale;
        }
        else if (name == "sepia")
        {
            fn.kind = FilterKind::Sepia;
        }
        else if (name == "invert")
        {
            fn.kind = FilterKind::Invert;
        }
        else if (name == "opacity")
        {
            fn.kind = FilterKind::Opacity;
        }
        else
        {
            return std::nullopt;  // blur(), drop-shadow(), url(), ...
        }

        if (arg.empty())
        {
            fn.amount = 1;
        }
        else if (arg.back() == '%')
        {
            const auto v = parseNumber(arg.substr(0, arg.size() - 1));
            if (!v)
            {
                return std::nullopt;
            }
            fn.amount = *v / 100.0;
        }
        else
        {
            const auto v = parseNumber(arg);
            if (!v)
            {
                return std::nullopt;
            }
            fn.amount = *v;
        }
        out.push_back(fn);
    }
    return out;
}

std::optional<Origin> parseOrigin(string_view text)
{
    const auto parts = splitSpace(trim(text));
    if (parts.empty() || parts.size() > 3)
    {
        return std::nullopt;
    }
    if (parts.size() == 3)
    {
        const auto z = parseLength(parts[2]);
        if (!z || z->value != 0)
        {
            return std::nullopt;
        }
    }

    enum class Axis { Any, X, Y };
    struct Parsed {
        Length value;
        Axis axis = Axis::Any;
        bool center = false;
    };
    const auto one = [](string_view token) -> std::optional<Parsed> {
        const string t = lower(token);
        Parsed p;
        if (t == "left")
        {
            p.value = {0, true};
            p.axis = Axis::X;
        }
        else if (t == "right")
        {
            p.value = {100, true};
            p.axis = Axis::X;
        }
        else if (t == "top")
        {
            p.value = {0, true};
            p.axis = Axis::Y;
        }
        else if (t == "bottom")
        {
            p.value = {100, true};
            p.axis = Axis::Y;
        }
        else if (t == "center")
        {
            p.value = {50, true};
            p.center = true;
        }
        else if (const auto l = parseLength(t))
        {
            p.value = *l;
        }
        else
        {
            return std::nullopt;
        }
        return p;
    };

    const auto first = one(parts[0]);
    if (!first)
    {
        return std::nullopt;
    }
    Origin out;
    if (parts.size() == 1)
    {
        if (first->axis == Axis::Y)
        {
            out.y = first->value;
        }
        else
        {
            out.x = first->value;
        }
        return out;
    }

    const auto second = one(parts[1]);
    if (!second)
    {
        return std::nullopt;
    }
    // `bottom left`, `center top`, ... may come in either order
    const bool swapped = first->axis == Axis::Y || second->axis == Axis::X;
    if ((first->axis == Axis::X && second->axis == Axis::X) ||
        (first->axis == Axis::Y && second->axis == Axis::Y))
    {
        return std::nullopt;
    }
    out.x = swapped ? second->value : first->value;
    out.y = swapped ? first->value : second->value;
    return out;
}

std::optional<Clip> parseClip(string_view text)
{
    text = trim(text);
    Clip out;
    if (lower(text) == "none")
    {
        return out;
    }
    const auto args = callArgs(text, "polygon");
    if (!args)
    {
        return std::nullopt;
    }
    auto points = splitTop(*args, ',');
    if (!points.empty())
    {
        const string first = lower(points.front());
        if (first == "nonzero" || first == "evenodd")
        {
            points.erase(points.begin());
        }
    }
    if (points.size() < 3)
    {
        return std::nullopt;
    }
    for (const auto point : points)
    {
        const auto xy = splitSpace(point);
        if (xy.size() != 2)
        {
            return std::nullopt;
        }
        const auto x = parseLength(xy[0]);
        const auto y = parseLength(xy[1]);
        if (!x || !y)
        {
            return std::nullopt;
        }
        out.points.emplace_back(*x, *y);
    }
    out.none = false;
    return out;
}

// ---------------------------------------------------------------------------
// Matrices
// ---------------------------------------------------------------------------

Matrix matrixOfFn(const TransformFn &fn, const Box &box)
{
    switch (fn.kind)
    {
        case TransformFn::Kind::Translate:
            return Matrix::translate(toPx(fn.x, box.width),
                                     toPx(fn.y, box.height));
        case TransformFn::Kind::Rotate:
            return Matrix::rotate(fn.n[0]);
        case TransformFn::Kind::Scale:
            return Matrix::scale(fn.n[0], fn.n[1]);
        case TransformFn::Kind::Skew:
            return Matrix::skew(fn.n[0], fn.n[1]);
        case TransformFn::Kind::Matrix:
            return Matrix{fn.n[0], fn.n[1], fn.n[2],
                          fn.n[3], fn.n[4], fn.n[5]};
    }
    return {};
}

Matrix matrixOfList(const TransformList &list, const Box &box)
{
    Matrix m;
    for (const auto &fn : list)
    {
        m = m * matrixOfFn(fn, box);
    }
    return m;
}

/// CSS Transforms 1, "Decomposing a 2D matrix", in the form
/// `translate * rotate * skew * scale`.
struct Decomposed {
    double tx = 0;
    double ty = 0;
    double angle = 0;  // radians
    double skew = 0;   // the `c` of matrix(1, 0, skew, 1, 0, 0)
    double sx = 1;
    double sy = 1;
};

std::optional<Decomposed> decompose(const Matrix &m)
{
    Decomposed out;
    out.tx = m.e;
    out.ty = m.f;
    out.sx = std::hypot(m.a, m.b);
    if (out.sx == 0)
    {
        return std::nullopt;
    }
    const double ux = m.a / out.sx;
    const double uy = m.b / out.sx;
    out.angle = std::atan2(uy, ux);

    const double dot = ux * m.c + uy * m.d;
    const double det = m.a * m.d - m.b * m.c;
    out.sy = det / out.sx;
    if (out.sy == 0)
    {
        return std::nullopt;
    }
    out.skew = dot / out.sy;
    return out;
}

Matrix recompose(const Decomposed &d)
{
    return Matrix::translate(d.tx, d.ty) * Matrix::rotate(d.angle * 180.0 / PI) *
           Matrix{1, 0, d.skew, 1, 0, 0} * Matrix::scale(d.sx, d.sy);
}

Matrix lerpMatrices(const Matrix &a, const Matrix &b, double t)
{
    const auto da = decompose(a);
    const auto db = decompose(b);
    if (!da || !db)
    {
        return t < 0.5 ? a : b;  // can't be interpolated
    }
    Decomposed x = *da;
    Decomposed y = *db;
    if (std::abs(x.angle - y.angle) > PI)
    {
        if (x.angle > y.angle)
        {
            y.angle += 2 * PI;
        }
        else
        {
            x.angle += 2 * PI;
        }
    }
    Decomposed out;
    out.tx = lerp(x.tx, y.tx, t);
    out.ty = lerp(x.ty, y.ty, t);
    out.angle = lerp(x.angle, y.angle, t);
    out.skew = lerp(x.skew, y.skew, t);
    out.sx = lerp(x.sx, y.sx, t);
    out.sy = lerp(x.sy, y.sy, t);
    return recompose(out);
}

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

TransformFn identityLike(const TransformFn &fn)
{
    TransformFn out;
    out.kind = fn.kind;
    switch (fn.kind)
    {
        case TransformFn::Kind::Translate:
            break;
        case TransformFn::Kind::Rotate:
        case TransformFn::Kind::Skew:
            break;
        case TransformFn::Kind::Scale:
            out.n[0] = 1;
            out.n[1] = 1;
            break;
        case TransformFn::Kind::Matrix:
            out.n = {1, 0, 0, 1, 0, 0};
            break;
    }
    return out;
}

TransformList lerpTransforms(const TransformList &from,
                             const TransformList &to, double t,
                             const Box &box)
{
    // The lists can be mixed if the shorter one is a prefix of the longer
    // one, function for function: it's padded with identities.
    const size_t common = std::min(from.size(), to.size());
    bool compatible = true;
    for (size_t i = 0; i < common; i++)
    {
        if (from[i].kind != to[i].kind ||
            from[i].kind == TransformFn::Kind::Matrix)
        {
            compatible = false;
            break;
        }
    }
    for (const auto *list : {&from, &to})
    {
        for (size_t i = common; i < list->size(); i++)
        {
            if ((*list)[i].kind == TransformFn::Kind::Matrix)
            {
                compatible = false;
            }
        }
    }

    if (!compatible)
    {
        const auto m = lerpMatrices(matrixOfList(from, box),
                                    matrixOfList(to, box), t);
        TransformFn fn;
        fn.kind = TransformFn::Kind::Matrix;
        fn.n = {m.a, m.b, m.c, m.d, m.e, m.f};
        return {fn};
    }

    TransformList out;
    const size_t count = std::max(from.size(), to.size());
    for (size_t i = 0; i < count; i++)
    {
        const TransformFn a =
            i < from.size() ? from[i] : identityLike(to[i]);
        const TransformFn b = i < to.size() ? to[i] : identityLike(from[i]);
        TransformFn fn;
        fn.kind = a.kind;
        if (a.kind == TransformFn::Kind::Translate)
        {
            fn.x = Length{lerp(toPx(a.x, box.width), toPx(b.x, box.width), t),
                          false};
            fn.y = Length{
                lerp(toPx(a.y, box.height), toPx(b.y, box.height), t), false};
        }
        else
        {
            for (size_t k = 0; k < fn.n.size(); k++)
            {
                fn.n[k] = lerp(a.n[k], b.n[k], t);
            }
        }
        out.push_back(fn);
    }
    return out;
}

double filterIdentity(FilterKind kind)
{
    switch (kind)
    {
        case FilterKind::Brightness:
        case FilterKind::Contrast:
        case FilterKind::Saturate:
        case FilterKind::Opacity:
            return 1;
        case FilterKind::HueRotate:
        case FilterKind::Grayscale:
        case FilterKind::Sepia:
        case FilterKind::Invert:
            return 0;
    }
    return 0;
}

FilterList lerpFilters(const FilterList &from, const FilterList &to, double t)
{
    const size_t common = std::min(from.size(), to.size());
    for (size_t i = 0; i < common; i++)
    {
        if (from[i].kind != to[i].kind)
        {
            return t < 0.5 ? from : to;  // can't be interpolated
        }
    }
    FilterList out;
    const size_t count = std::max(from.size(), to.size());
    for (size_t i = 0; i < count; i++)
    {
        const FilterKind kind = i < from.size() ? from[i].kind : to[i].kind;
        const double a =
            i < from.size() ? from[i].amount : filterIdentity(kind);
        const double b = i < to.size() ? to[i].amount : filterIdentity(kind);
        out.push_back(FilterFn{kind, lerp(a, b, t)});
    }
    return out;
}

/// Lengths that may be `auto`. `auto` can't be interpolated: the value
/// flips halfway. A percentage and a px value are both brought to px (against
/// `reference`) unless `mixUnits` is false.
Value interpolateLenOrAuto(const Value &from, const Value &to, double t,
                           double reference, bool mixUnits)
{
    const auto &a = std::get<LenOrAuto>(from);
    const auto &b = std::get<LenOrAuto>(to);
    if (a.isAuto || b.isAuto)
    {
        return t < 0.5 ? from : to;
    }
    if (a.len.percent == b.len.percent)
    {
        return LenOrAuto{false,
                         Length{lerp(a.len.value, b.len.value, t),
                                a.len.percent}};
    }
    if (!mixUnits)
    {
        return t < 0.5 ? from : to;
    }
    return LenOrAuto{false, Length{lerp(toPx(a.len, reference),
                                        toPx(b.len, reference), t),
                                   false}};
}

Shadow blendShadows(const Shadow &a, const Shadow &b, double t)
{
    Shadow out;
    out.inset = a.inset;
    out.x = lerp(a.x, b.x, t);
    out.y = lerp(a.y, b.y, t);
    out.blur = lerp(a.blur, b.blur, t);
    out.spread = lerp(a.spread, b.spread, t);
    // colours are blended premultiplied, as CSS says
    const double alpha = lerp(a.rgba[3], b.rgba[3], t);
    out.rgba[3] = alpha;
    for (size_t i = 0; i < 3; i++)
    {
        const double premultiplied =
            lerp(a.rgba[i] * a.rgba[3], b.rgba[i] * b.rgba[3], t);
        out.rgba[i] = alpha > 0 ? premultiplied / alpha : 0;
    }
    return out;
}

/// The shorter list is padded with transparent shadows. Lists whose
/// shadows don't pair up (inset against outer) flip halfway.
Value interpolateShadows(const Value &from, const Value &to, double t)
{
    auto a = std::get<ShadowList>(from);
    auto b = std::get<ShadowList>(to);
    const auto invisibleLike = [](const Shadow &other) {
        Shadow pad;
        pad.inset = other.inset;
        pad.rgba = {other.rgba[0], other.rgba[1], other.rgba[2], 0};
        return pad;
    };
    while (a.size() < b.size())
    {
        a.push_back(invisibleLike(b[a.size()]));
    }
    while (b.size() < a.size())
    {
        b.push_back(invisibleLike(a[b.size()]));
    }
    ShadowList out;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (a[i].inset != b[i].inset)
        {
            return t < 0.5 ? from : to;
        }
        out.push_back(blendShadows(a[i], b[i], t));
    }
    return out;
}

Value interpolate(Prop prop, const Value &from, const Value &to, double t,
                  const Box &box)
{
    switch (prop)
    {
        case Prop::Transform:
        case Prop::Translate:
        case Prop::Rotate:
        case Prop::Scale:
            return lerpTransforms(std::get<TransformList>(from),
                                  std::get<TransformList>(to), t, box);
        case Prop::Opacity:
            return lerp(std::get<double>(from), std::get<double>(to), t);
        case Prop::Filter:
            return lerpFilters(std::get<FilterList>(from),
                               std::get<FilterList>(to), t);
        case Prop::Origin: {
            const auto &a = std::get<Origin>(from);
            const auto &b = std::get<Origin>(to);
            Origin out;
            out.x = Length{lerp(toPx(a.x, box.width), toPx(b.x, box.width), t),
                           false};
            out.y = Length{
                lerp(toPx(a.y, box.height), toPx(b.y, box.height), t), false};
            return out;
        }
        case Prop::Clip: {
            const auto &a = std::get<Clip>(from);
            const auto &b = std::get<Clip>(to);
            if (a.none || b.none || a.points.size() != b.points.size())
            {
                return t < 0.5 ? from : to;  // can't be interpolated
            }
            Clip out;
            out.none = false;
            for (size_t i = 0; i < a.points.size(); i++)
            {
                out.points.emplace_back(
                    Length{lerp(toPx(a.points[i].first, box.width),
                                toPx(b.points[i].first, box.width), t),
                           false},
                    Length{lerp(toPx(a.points[i].second, box.height),
                                toPx(b.points[i].second, box.height), t),
                           false});
            }
            return out;
        }
        case Prop::Left:
        case Prop::Right:
        case Prop::Width:
            return interpolateLenOrAuto(from, to, t, box.width, true);
        case Prop::Top:
        case Prop::Bottom:
        case Prop::Height:
            return interpolateLenOrAuto(from, to, t, box.height, true);
        case Prop::LineHeight:
            return interpolateLenOrAuto(from, to, t, 0, false);
        case Prop::FontSize:
        case Prop::LetterSpacing:
            return lerp(std::get<double>(from), std::get<double>(to), t);
        case Prop::ContentText:
            return t < 0.5 ? from : to;
        case Prop::BoxShadow:
            return interpolateShadows(from, to, t);
    }
    return from;
}

constexpr Prop ALL_PROPS[] = {
    Prop::Transform, Prop::Translate, Prop::Rotate, Prop::Scale,
    Prop::Opacity,   Prop::Filter,    Prop::Origin, Prop::Clip,
    Prop::Left,      Prop::Right,     Prop::Top,    Prop::Bottom,
    Prop::Width,     Prop::Height,    Prop::FontSize,
    Prop::LineHeight, Prop::LetterSpacing, Prop::ContentText,
    Prop::BoxShadow,
};

/// Declarations that are allowed in keyframes without doing anything.
bool isIgnorableKeyframeProperty(string_view name)
{
    return name == "animation-timing-function" || name == "z-index" ||
           name == "will-change" || name == "pointer-events" ||
           name == "mix-blend-mode" ||
           startsWith(name, "-") || startsWith(name, "transition");
}

}  // namespace

// ---------------------------------------------------------------------------
// Matrix
// ---------------------------------------------------------------------------

std::optional<std::array<double, 4>> parseColorValue(string_view text)
{
    return parseColor(text);
}

Matrix Matrix::translate(double x, double y)
{
    return Matrix{1, 0, 0, 1, x, y};
}

Matrix Matrix::rotate(double degrees)
{
    const double rad = degrees * PI / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    return Matrix{c, s, -s, c, 0, 0};
}

Matrix Matrix::scale(double x, double y)
{
    return Matrix{x, 0, 0, y, 0, 0};
}

Matrix Matrix::skew(double xDegrees, double yDegrees)
{
    return Matrix{1, std::tan(yDegrees * PI / 180.0),
                  std::tan(xDegrees * PI / 180.0), 1, 0, 0};
}

Matrix operator*(const Matrix &l, const Matrix &r)
{
    return Matrix{
        l.a * r.a + l.c * r.b,
        l.b * r.a + l.d * r.b,
        l.a * r.c + l.c * r.d,
        l.b * r.c + l.d * r.d,
        l.a * r.e + l.c * r.f + l.e,
        l.b * r.e + l.d * r.f + l.f,
    };
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

std::optional<TimingFunction> parseTimingFunction(string_view text)
{
    text = trim(text);
    const string t = lower(text);
    TimingFunction out;
    const auto bezier = [&](double x1, double y1, double x2, double y2) {
        out.kind = TimingFunction::Kind::Bezier;
        out.x1 = x1;
        out.y1 = y1;
        out.x2 = x2;
        out.y2 = y2;
        return out;
    };

    if (t == "ease")
    {
        return bezier(0.25, 0.1, 0.25, 1.0);
    }
    if (t == "linear")
    {
        return bezier(0, 0, 1, 1);
    }
    if (t == "ease-in")
    {
        return bezier(0.42, 0, 1, 1);
    }
    if (t == "ease-out")
    {
        return bezier(0, 0, 0.58, 1);
    }
    if (t == "ease-in-out")
    {
        return bezier(0.42, 0, 0.58, 1);
    }
    if (t == "step-start" || t == "step-end")
    {
        out.kind = TimingFunction::Kind::Steps;
        out.steps = 1;
        out.jump = t == "step-start" ? TimingFunction::Jump::Start
                                     : TimingFunction::Jump::End;
        return out;
    }
    if (const auto args = callArgs(text, "cubic-bezier"))
    {
        const auto parts = splitTop(*args, ',');
        if (parts.size() != 4)
        {
            return std::nullopt;
        }
        double v[4];
        for (size_t i = 0; i < 4; i++)
        {
            const auto n = parseNumber(parts[i]);
            if (!n)
            {
                return std::nullopt;
            }
            v[i] = *n;
        }
        if (v[0] < 0 || v[0] > 1 || v[2] < 0 || v[2] > 1)
        {
            return std::nullopt;  // x must be in 0..1
        }
        return bezier(v[0], v[1], v[2], v[3]);
    }
    if (const auto args = callArgs(text, "steps"))
    {
        const auto parts = splitTop(*args, ',');
        if (parts.empty() || parts.size() > 2)
        {
            return std::nullopt;
        }
        const auto count = parseNumber(parts[0]);
        if (!count || *count < 1 || *count != std::floor(*count) ||
            *count > 100000)
        {
            return std::nullopt;
        }
        out.kind = TimingFunction::Kind::Steps;
        out.steps = static_cast<int>(*count);
        out.jump = TimingFunction::Jump::End;
        if (parts.size() == 2)
        {
            const string j = lower(parts[1]);
            if (j == "jump-start" || j == "start")
            {
                out.jump = TimingFunction::Jump::Start;
            }
            else if (j == "jump-end" || j == "end")
            {
                out.jump = TimingFunction::Jump::End;
            }
            else if (j == "jump-none")
            {
                out.jump = TimingFunction::Jump::None;
                if (out.steps < 2)
                {
                    return std::nullopt;
                }
            }
            else if (j == "jump-both")
            {
                out.jump = TimingFunction::Jump::Both;
            }
            else
            {
                return std::nullopt;
            }
        }
        return out;
    }
    return std::nullopt;
}

double applyTiming(const TimingFunction &timing, double progress)
{
    if (timing.kind == TimingFunction::Kind::Steps)
    {
        const double p = clamp(progress, 0.0, 1.0);
        double current = std::floor(p * timing.steps);
        double steps = timing.steps;
        if (timing.jump == TimingFunction::Jump::Start ||
            timing.jump == TimingFunction::Jump::Both)
        {
            current += 1;
        }
        if (timing.jump == TimingFunction::Jump::Both)
        {
            steps += 1;
        }
        else if (timing.jump == TimingFunction::Jump::None)
        {
            steps -= 1;
        }
        current = clamp(current, 0.0, steps);
        return steps > 0 ? current / steps : 0.0;
    }

    // cubic Bezier through (0,0), (x1,y1), (x2,y2), (1,1): find the curve
    // parameter whose x is `progress`, return its y
    if (progress <= 0)
    {
        return 0.0;
    }
    if (progress >= 1)
    {
        return 1.0;
    }
    if (timing.x1 == timing.y1 && timing.x2 == timing.y2)
    {
        return progress;  // linear
    }

    const auto curve = [](double a, double b, double s) {
        // coordinate of the Bezier at parameter s: 3(1-s)^2 s a + 3(1-s) s^2 b + s^3
        const double inv = 1 - s;
        return 3 * inv * inv * s * a + 3 * inv * s * s * b + s * s * s;
    };
    double lo = 0;
    double hi = 1;
    double s = progress;
    for (int i = 0; i < 60; i++)
    {
        const double x = curve(timing.x1, timing.x2, s);
        if (std::abs(x - progress) < 1e-9)
        {
            break;
        }
        if (x < progress)
        {
            lo = s;
        }
        else
        {
            hi = s;
        }
        s = (lo + hi) / 2;
    }
    return curve(timing.y1, timing.y2, s);
}

// ---------------------------------------------------------------------------
// Animation lists and keyframes
// ---------------------------------------------------------------------------

std::optional<std::vector<AnimationSpec>> parseAnimationList(string_view value)
{
    std::vector<AnimationSpec> out;
    for (const auto entry : splitTop(value, ','))
    {
        AnimationSpec spec;
        bool haveDuration = false;
        bool haveName = false;
        bool none = false;

        for (const auto token : splitSpace(entry))
        {
            const string t = lower(token);
            if (startsWith(t, "calc(") || startsWith(t, "var("))
            {
                return std::nullopt;
            }
            if (const auto time = parseTimeMs(token))
            {
                if (!haveDuration)
                {
                    spec.durationMs = *time;
                    haveDuration = true;
                }
                else
                {
                    spec.delayMs = *time;
                }
            }
            else if (const auto timing = parseTimingFunction(token))
            {
                spec.timing = *timing;
            }
            else if (t == "infinite")
            {
                spec.iterations = INF;
            }
            else if (const auto n = parseNumber(token))
            {
                if (*n < 0)
                {
                    return std::nullopt;
                }
                spec.iterations = *n;
            }
            else if (t == "normal")
            {
                spec.direction = AnimationSpec::Direction::Normal;
            }
            else if (t == "reverse")
            {
                spec.direction = AnimationSpec::Direction::Reverse;
            }
            else if (t == "alternate")
            {
                spec.direction = AnimationSpec::Direction::Alternate;
            }
            else if (t == "alternate-reverse")
            {
                spec.direction = AnimationSpec::Direction::AlternateReverse;
            }
            else if (t == "forwards")
            {
                spec.fill = AnimationSpec::Fill::Forwards;
            }
            else if (t == "backwards")
            {
                spec.fill = AnimationSpec::Fill::Backwards;
            }
            else if (t == "both")
            {
                spec.fill = AnimationSpec::Fill::Both;
            }
            else if (t == "running" || t == "paused")
            {
                // not relevant to a baked animation
            }
            else if (t == "none")
            {
                none = true;
            }
            else
            {
                spec.name = string(token);
                haveName = true;
            }
        }

        if (none && !haveName)
        {
            continue;  // `animation: none`
        }
        if (!haveName)
        {
            return std::nullopt;
        }
        out.push_back(std::move(spec));
    }
    return out;
}

std::optional<std::pair<Prop, Value>> parseDeclaration(string_view name,
                                                       string_view value)
{
    const string n = lower(trim(name));
    value = trim(value);
    if (value.empty() || lower(value).find("var(") != string::npos)
    {
        return std::nullopt;
    }

    if (n == "transform")
    {
        if (auto v = parseTransformList(value))
        {
            return std::make_pair(Prop::Transform, Value(std::move(*v)));
        }
    }
    else if (n == "translate" || n == "rotate" || n == "scale")
    {
        const Prop prop = n == "translate" ? Prop::Translate
                          : n == "rotate"  ? Prop::Rotate
                                           : Prop::Scale;
        if (auto v = parseIndividual(prop, value))
        {
            return std::make_pair(prop, Value(std::move(*v)));
        }
    }
    else if (n == "opacity")
    {
        std::optional<double> v;
        if (value.back() == '%')
        {
            if (const auto p = parseNumber(value.substr(0, value.size() - 1)))
            {
                v = *p / 100.0;
            }
        }
        else
        {
            v = parseNumber(value);
        }
        if (v)
        {
            return std::make_pair(Prop::Opacity,
                                  Value(clamp(*v, 0.0, 1.0)));
        }
    }
    else if (n == "filter")
    {
        if (auto v = parseFilterList(value))
        {
            return std::make_pair(Prop::Filter, Value(std::move(*v)));
        }
    }
    else if (n == "transform-origin")
    {
        if (const auto v = parseOrigin(value))
        {
            return std::make_pair(Prop::Origin, Value(*v));
        }
    }
    else if (n == "clip-path")
    {
        if (auto v = parseClip(value))
        {
            return std::make_pair(Prop::Clip, Value(std::move(*v)));
        }
    }
    else if (n == "left" || n == "right" || n == "top" || n == "bottom" ||
             n == "width" || n == "height")
    {
        const Prop prop = n == "left"     ? Prop::Left
                          : n == "right"  ? Prop::Right
                          : n == "top"    ? Prop::Top
                          : n == "bottom" ? Prop::Bottom
                          : n == "width"  ? Prop::Width
                                          : Prop::Height;
        if (const auto v = parseLenOrAuto(value))
        {
            return std::make_pair(prop, Value(*v));
        }
    }
    else if (n == "font-size" || n == "letter-spacing")
    {
        if (n == "letter-spacing" && lower(value) == "normal")
        {
            return std::make_pair(Prop::LetterSpacing, Value(0.0));
        }
        if (const auto v = parseLength(value); v && !v->percent)
        {
            if (n == "font-size")
            {
                if (v->value > 0)
                {
                    return std::make_pair(Prop::FontSize, Value(v->value));
                }
            }
            else
            {
                return std::make_pair(Prop::LetterSpacing, Value(v->value));
            }
        }
    }
    else if (n == "line-height")
    {
        if (const auto v = parseLineHeight(value))
        {
            return std::make_pair(Prop::LineHeight, Value(*v));
        }
    }
    else if (n == "content")
    {
        if (auto v = parseContent(value))
        {
            return std::make_pair(Prop::ContentText, Value(std::move(*v)));
        }
    }
    else if (n == "box-shadow")
    {
        if (auto v = parseShadowList(value))
        {
            return std::make_pair(Prop::BoxShadow, Value(std::move(*v)));
        }
    }
    return std::nullopt;
}

Value getProp(const Computed &state, Prop prop)
{
    switch (prop)
    {
        case Prop::Transform:
            return state.transform;
        case Prop::Translate:
            return state.translate;
        case Prop::Rotate:
            return state.rotate;
        case Prop::Scale:
            return state.scale;
        case Prop::Opacity:
            return state.opacity;
        case Prop::Filter:
            return state.filter;
        case Prop::Origin:
            return state.origin;
        case Prop::Clip:
            return state.clip;
        case Prop::Left:
            return state.left;
        case Prop::Right:
            return state.right;
        case Prop::Top:
            return state.top;
        case Prop::Bottom:
            return state.bottom;
        case Prop::Width:
            return state.width;
        case Prop::Height:
            return state.height;
        case Prop::FontSize:
            return state.fontSize;
        case Prop::LineHeight:
            return state.lineHeight;
        case Prop::LetterSpacing:
            return state.letterSpacing;
        case Prop::ContentText:
            return state.content;
        case Prop::BoxShadow:
            return state.boxShadow;
    }
    return state.transform;
}

void setProp(Computed &state, Prop prop, Value value)
{
    switch (prop)
    {
        case Prop::Transform:
            state.transform = std::get<TransformList>(std::move(value));
            break;
        case Prop::Translate:
            state.translate = std::get<TransformList>(std::move(value));
            break;
        case Prop::Rotate:
            state.rotate = std::get<TransformList>(std::move(value));
            break;
        case Prop::Scale:
            state.scale = std::get<TransformList>(std::move(value));
            break;
        case Prop::Opacity:
            state.opacity = std::get<double>(value);
            break;
        case Prop::Filter:
            state.filter = std::get<FilterList>(std::move(value));
            break;
        case Prop::Origin:
            state.origin = std::get<Origin>(value);
            break;
        case Prop::Clip:
            state.clip = std::get<Clip>(std::move(value));
            break;
        case Prop::Left:
            state.left = std::get<LenOrAuto>(value);
            break;
        case Prop::Right:
            state.right = std::get<LenOrAuto>(value);
            break;
        case Prop::Top:
            state.top = std::get<LenOrAuto>(value);
            break;
        case Prop::Bottom:
            state.bottom = std::get<LenOrAuto>(value);
            break;
        case Prop::Width:
            state.width = std::get<LenOrAuto>(value);
            break;
        case Prop::Height:
            state.height = std::get<LenOrAuto>(value);
            break;
        case Prop::FontSize:
            state.fontSize = std::get<double>(value);
            break;
        case Prop::LineHeight:
            state.lineHeight = std::get<LenOrAuto>(value);
            break;
        case Prop::LetterSpacing:
            state.letterSpacing = std::get<double>(value);
            break;
        case Prop::ContentText:
            state.content = std::get<Content>(std::move(value));
            break;
        case Prop::BoxShadow:
            state.boxShadow = std::get<ShadowList>(std::move(value));
            break;
    }
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

std::optional<Animation> Animation::build(const AnimationSpec &spec,
                                          const std::vector<Keyframe> &keyframes,
                                          string &problem)
{
    Animation out;
    out.spec_ = spec;

    // stable, by offset
    std::vector<const Keyframe *> sorted;
    for (const auto &frame : keyframes)
    {
        if (frame.offset < 0 || frame.offset > 1)
        {
            continue;
        }
        sorted.push_back(&frame);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Keyframe *a, const Keyframe *b) {
                         return a->offset < b->offset;
                     });

    for (const auto *frame : sorted)
    {
        std::optional<TimingFunction> timing;
        for (const auto &decl : frame->decls)
        {
            if (lower(decl.name) == "animation-timing-function")
            {
                timing = parseTimingFunction(decl.value);
            }
        }

        for (const auto &decl : frame->decls)
        {
            const string name = lower(trim(decl.name));
            if (isIgnorableKeyframeProperty(name))
            {
                continue;
            }
            auto parsed = parseDeclaration(name, decl.value);
            if (!parsed)
            {
                problem = "keyframe property " + name + ": " + decl.value;
                return std::nullopt;
            }

            auto &[prop, value] = *parsed;
            auto track = std::find_if(
                out.tracks_.begin(), out.tracks_.end(),
                [&](const auto &entry) { return entry.first == prop; });
            if (track == out.tracks_.end())
            {
                out.tracks_.emplace_back(prop, std::vector<Stop>{});
                track = std::prev(out.tracks_.end());
            }
            auto &stops = track->second;
            if (!stops.empty() && stops.back().offset == frame->offset)
            {
                stops.back().value = std::move(value);
                stops.back().timing = timing;
            }
            else
            {
                stops.push_back(Stop{frame->offset, std::move(value), timing});
            }
        }
    }

    if (out.tracks_.empty())
    {
        problem = "no property it can animate";
        return std::nullopt;
    }
    return out;
}

bool Animation::animates(Prop prop) const
{
    return std::any_of(this->tracks_.begin(), this->tracks_.end(),
                       [&](const auto &entry) { return entry.first == prop; });
}

std::optional<double> Animation::progressAt(double timeMs) const
{
    const double d = this->spec_.durationMs;
    const double iterations = this->spec_.iterations;
    if (d <= 0 || iterations <= 0)
    {
        return std::nullopt;
    }

    const bool fillBackwards = this->spec_.fill == AnimationSpec::Fill::Backwards ||
                               this->spec_.fill == AnimationSpec::Fill::Both;
    const bool fillForwards = this->spec_.fill == AnimationSpec::Fill::Forwards ||
                              this->spec_.fill == AnimationSpec::Fill::Both;

    const double t = timeMs - this->spec_.delayMs;
    double iteration = 0;
    double local = 0;
    if (t < 0)
    {
        if (!fillBackwards)
        {
            return std::nullopt;
        }
    }
    else if (iterations != INF && t >= d * iterations)
    {
        if (!fillForwards)
        {
            return std::nullopt;
        }
        const double whole = std::floor(iterations);
        const double fraction = iterations - whole;
        if (fraction == 0)
        {
            iteration = whole - 1;
            local = 1;
        }
        else
        {
            iteration = whole;
            local = fraction;
        }
    }
    else
    {
        iteration = std::floor(t / d);
        local = (t - iteration * d) / d;
    }

    const auto odd = [&] { return std::fmod(iteration, 2.0) == 1.0; };
    bool reversed = false;
    switch (this->spec_.direction)
    {
        case AnimationSpec::Direction::Normal:
            break;
        case AnimationSpec::Direction::Reverse:
            reversed = true;
            break;
        case AnimationSpec::Direction::Alternate:
            reversed = odd();
            break;
        case AnimationSpec::Direction::AlternateReverse:
            reversed = !odd();
            break;
    }
    return reversed ? 1.0 - local : local;
}

Value Animation::valueAt(Prop prop, double progress, const Computed &base,
                         const Box &box) const
{
    const auto track = std::find_if(
        this->tracks_.begin(), this->tracks_.end(),
        [&](const auto &entry) { return entry.first == prop; });
    if (track == this->tracks_.end())
    {
        return getProp(base, prop);
    }

    // stops, with the implicit ones (the element's own value) at both ends
    std::vector<Stop> stops;
    stops.reserve(track->second.size() + 2);
    if (track->second.front().offset > 0)
    {
        stops.push_back(Stop{0, getProp(base, prop), std::nullopt});
    }
    stops.insert(stops.end(), track->second.begin(), track->second.end());
    if (stops.back().offset < 1)
    {
        stops.push_back(Stop{1, getProp(base, prop), std::nullopt});
    }

    if (progress <= stops.front().offset)
    {
        return stops.front().value;
    }
    if (progress >= stops.back().offset)
    {
        return stops.back().value;
    }
    size_t i = 0;
    while (i + 2 < stops.size() && progress >= stops[i + 1].offset)
    {
        i++;
    }
    const Stop &a = stops[i];
    const Stop &b = stops[i + 1];
    const double span = b.offset - a.offset;
    const double local = span > 0 ? (progress - a.offset) / span : 1.0;
    const double eased =
        applyTiming(a.timing.value_or(this->spec_.timing), local);
    return interpolate(prop, a.value, b.value, eased, box);
}

Computed computeAt(const std::vector<Animation> &animations,
                   const Computed &base, double timeMs, const Box &box)
{
    Computed out = base;
    for (const Prop prop : ALL_PROPS)
    {
        for (const auto &animation : animations)
        {
            if (!animation.animates(prop))
            {
                continue;
            }
            if (const auto progress = animation.progressAt(timeMs))
            {
                setProp(out, prop,
                        animation.valueAt(prop, *progress, base, box));
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

std::pair<double, double> originPx(const Origin &origin, const Box &box)
{
    return {toPx(origin.x, box.width), toPx(origin.y, box.height)};
}

Matrix matrixWithoutOrigin(const Computed &state, const Box &box)
{
    // CSS: translate, rotate and scale apply first (outermost), then
    // `transform`
    return matrixOfList(state.translate, box) *
           matrixOfList(state.rotate, box) * matrixOfList(state.scale, box) *
           matrixOfList(state.transform, box);
}

Matrix matrixOf(const Computed &state, const Box &box)
{
    const Matrix m = matrixWithoutOrigin(state, box);
    const auto [ox, oy] = originPx(state.origin, box);
    return Matrix::translate(ox, oy) * m * Matrix::translate(-ox, -oy);
}

std::vector<std::pair<double, double>> clipPx(const Clip &clip, const Box &box)
{
    std::vector<std::pair<double, double>> out;
    if (clip.none)
    {
        return out;
    }
    for (const auto &[x, y] : clip.points)
    {
        out.emplace_back(toPx(x, box.width), toPx(y, box.height));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Baking
// ---------------------------------------------------------------------------

std::optional<Baked> bake(const std::vector<Animation> &animations,
                          const Computed &base, const Box &box,
                          const BakeOptions &options, string &problem)
{
    if (animations.empty())
    {
        problem = "no animation";
        return std::nullopt;
    }
    if (options.stepMs <= 0)
    {
        problem = "bad step";
        return std::nullopt;
    }

    const auto &first = animations.front().spec();
    const bool anyInfinite =
        std::any_of(animations.begin(), animations.end(),
                    [](const Animation &a) { return a.spec().infinite(); });
    const bool allInfinite =
        std::all_of(animations.begin(), animations.end(),
                    [](const Animation &a) { return a.spec().infinite(); });

    // Animations that all repeat the same cycle are baked as one cycle that
    // is played that often. That keeps `0.3s 50` (15 s) or `5s 20` (100 s)
    // small and exact. Everything else is baked as one long timeline.
    const bool sameCycle = std::all_of(
        animations.begin(), animations.end(), [&](const Animation &a) {
            const auto &s = a.spec();
            return s.durationMs == first.durationMs && s.delayMs == 0 &&
                   s.direction == AnimationSpec::Direction::Normal &&
                   s.iterations == first.iterations;
        });
    const bool wholeCount =
        first.infinite() || first.iterations == std::floor(first.iterations);
    const bool periodic = sameCycle && wholeCount && first.iterations > 1;

    if (anyInfinite && !(allInfinite && periodic))
    {
        problem = "infinite animation that doesn't repeat one cycle";
        return std::nullopt;
    }

    Baked out;
    double total = 0;          // how much time is baked
    double finishedAt = 0;     // when the CSS is done
    if (periodic)
    {
        if (first.durationMs > options.maxMs)
        {
            problem = "cycle too long";
            return std::nullopt;
        }
        total = first.durationMs;
        out.plays = first.infinite() ? 0 : static_cast<int>(first.iterations);
        finishedAt = first.infinite() ? 0 : first.durationMs * first.iterations;
    }
    else
    {
        for (const auto &animation : animations)
        {
            const auto &s = animation.spec();
            if (s.durationMs <= 0)
            {
                continue;
            }
            finishedAt = std::max(
                finishedAt, std::max(0.0, s.delayMs) + s.durationMs * s.iterations);
        }
        total = finishedAt;
        if (total > options.maxMs)
        {
            total = options.maxMs;
            out.truncated = true;
        }
        out.plays = 1;
    }
    if (total <= 0)
    {
        problem = "no duration";
        return std::nullopt;
    }

    const int count = static_cast<int>(std::ceil(total / options.stepMs - 1e-9));
    out.frames.reserve(static_cast<size_t>(count));
    for (int k = 0; k < count; k++)
    {
        const double t = k * options.stepMs;
        const double span = std::min(options.stepMs, total - t);
        const int duration = std::max(1, static_cast<int>(std::lround(span)));
        Computed state = computeAt(animations, base, t, box);
        if (!out.frames.empty() && out.frames.back().state == state)
        {
            out.frames.back().durationMs += duration;
        }
        else
        {
            out.frames.push_back(BakedFrame{std::move(state), duration});
        }
    }

    out.rest = out.plays == 0 ? out.frames.back().state
                              : computeAt(animations, base, finishedAt, box);

    // does anything show?
    if (out.frames.size() == 1 && out.frames.front().state == out.rest)
    {
        problem = "nothing changes";
        return std::nullopt;
    }
    return out;
}

std::optional<BakedLayers> bakeLayers(const std::vector<LayerInput> &layers,
                                      const Box &box,
                                      const BakeOptions &options,
                                      string &problem)
{
    if (layers.empty())
    {
        problem = "no layers";
        return std::nullopt;
    }
    if (options.stepMs <= 0)
    {
        problem = "bad step";
        return std::nullopt;
    }

    double total = 0;
    bool any = false;
    for (const auto &layer : layers)
    {
        for (const auto &animation : layer.animations)
        {
            const auto &spec = animation.spec();
            if (spec.infinite())
            {
                problem = "infinite animation";
                return std::nullopt;
            }
            if (spec.durationMs <= 0 || spec.iterations <= 0)
            {
                continue;
            }
            any = true;
            // a negative delay starts the animation part way through
            total = std::max(total,
                             spec.delayMs + spec.durationMs * spec.iterations);
        }
    }
    if (!any || total <= 0)
    {
        problem = "no duration";
        return std::nullopt;
    }

    BakedLayers out;
    if (total > options.maxMs)
    {
        total = options.maxMs;
        out.truncated = true;
    }

    const int count = static_cast<int>(std::ceil(total / options.stepMs - 1e-9));
    out.frames.reserve(static_cast<size_t>(count));
    for (int k = 0; k < count; k++)
    {
        const double t = k * options.stepMs;
        const double span = std::min(options.stepMs, total - t);
        const int duration = std::max(1, static_cast<int>(std::lround(span)));

        BakedLayerFrame frame;
        frame.durationMs = duration;
        frame.layers.reserve(layers.size());
        for (const auto &layer : layers)
        {
            frame.layers.push_back(
                computeAt(layer.animations, layer.base, t, box));
        }
        if (!out.frames.empty() && out.frames.back().layers == frame.layers)
        {
            out.frames.back().durationMs += duration;
        }
        else
        {
            out.frames.push_back(std::move(frame));
        }
    }

    for (const auto &layer : layers)
    {
        out.rest.push_back(computeAt(layer.animations, layer.base, total, box));
    }

    if (out.frames.size() == 1 && out.frames.front().layers == out.rest)
    {
        problem = "nothing changes";
        return std::nullopt;
    }
    return out;
}

}  // namespace chatterino::dggcss::anim
