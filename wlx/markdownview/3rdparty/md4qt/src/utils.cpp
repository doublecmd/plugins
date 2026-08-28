/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "utils.h"
#include "block_parser.h"
#include "constants.h"
#include "entities_map.h"
#include "reverse_solidus.h"

namespace MD
{

QString readLinkDestination(Line &line)
{
    if (line.position() < line.length()) {
        ReverseSolidusHandler reverseSolidus;
        QString url;

        if (line.currentChar() == s_lessSignChar) {
            url = line.currentChar();
            line.nextChar();

            while (line.position() < line.length()) {
                url.append(line.currentChar());

                if (reverseSolidus.isNotEscaped(line.currentChar())) {
                    if (line.currentChar() == s_lessSignChar) {
                        return QString();
                    } else if (line.currentChar() == s_greaterSignChar) {
                        line.nextChar();
                        return url;
                    }
                }

                line.nextChar();
                reverseSolidus.next();
            }

            return QString();
        } else {
            int pc = 0;

            while (line.position() < line.length()) {
                url.append(line.currentChar());

                if (reverseSolidus.isNotEscaped(line.currentChar())) {
                    if (isAsciiControl(line.currentChar())) {
                        return QString();
                    } else if (line.currentChar() == s_spaceChar) {
                        if (!pc) {
                            return url;
                        } else {
                            return QString();
                        }
                    } else if (line.currentChar() == s_leftParenthesisChar) {
                        ++pc;
                    } else if (line.currentChar() == s_rightParenthesisChar) {
                        if (!pc) {
                            return url;
                        } else {
                            --pc;
                        }
                    }
                }

                line.nextChar();
                reverseSolidus.next();
            }

            return url;
        }
    } else {
        return QString();
    }
}

void removeBackslashes(QString &str)
{
    ReverseSolidusHandler reverseSolidus;

    for (qsizetype i = 0; i < str.length(); ++i) {
        if (reverseSolidus.isEscaped(str[i], i != str.length() - 1) && s_canBeEscaped.contains(str[i])) {
            str.remove(--i, 1);
        }

        reverseSolidus.next();
    }
}

void replaceEntity(QString &str)
{
    qsizetype p1 = 0;

    while ((p1 = str.indexOf(s_ampersandChar, p1)) != -1) {
        if (p1 > 0 && str[p1 - 1] == s_reverseSolidusChar) {
            ++p1;

            continue;
        }

        const auto p2 = str.indexOf(s_semicolonChar, p1);

        if (p2 != -1) {
            const auto en = str.sliced(p1, p2 - p1 + 1);

            if (en.size() > 2 && en[1] == s_numberSignChar) {
                if (en.size() > 3 && en[2].toLower() == s_xChar) {
                    const auto hex = en.sliced(3, en.size() - 4);

                    if (hex.size() <= 6 && hex.size() > 0) {
                        bool ok = false;

                        const char32_t c = hex.toInt(&ok, 16);

                        if (ok) {
                            if (c) {
                                const auto value = QString(QChar::fromUcs4(c));
                                str.replace(p1, p2 - p1 + 1, value);
                                p1 += value.length() - 1;
                            } else {
                                str.replace(p1, p2 - p1 + 1, s_replaceChar);
                            }
                        }
                    }
                } else {
                    const auto dec = en.sliced(2, en.size() - 3);

                    if (dec.size() <= 7 && dec.size() > 0) {
                        bool ok = false;

                        const char32_t c = dec.toInt(&ok, 10);

                        if (ok) {
                            if (c) {
                                const auto value = QString(QChar::fromUcs4(c));
                                str.replace(p1, p2 - p1 + 1, value);
                                p1 += value.length() - 1;
                            } else {
                                str.replace(p1, p2 - p1 + 1, s_replaceChar);
                            }
                        }
                    }
                }
            } else {
                const auto it = s_entityMap.find(en);

                if (it != s_entityMap.cend()) {
                    str.replace(p1, p2 - p1 + 1, *it);
                    p1 += it->length() - 1;
                }
            }

            ++p1;
        } else {
            break;
        }
    }
}

QString readLinkTitle(Line &line,
                      QChar &startChar,
                      int &startParenthesisCount,
                      qsizetype &startPos,
                      bool &endStarted)
{
    skipSpaces(line);

    if (line.position() < line.length()) {
        if (startChar.isNull()) {
            if (line.currentChar() != s_quotationMarkChar
                && line.currentChar() != s_apostropheChar
                && line.currentChar() != s_leftParenthesisChar) {
                return QString();
            }

            startChar = line.currentChar();
            startPos = line.position();

            while (line.currentChar() == s_leftParenthesisChar) {
                ++startParenthesisCount;
                line.nextChar();
            }

            if (startChar != s_leftParenthesisChar) {
                line.nextChar();
                startParenthesisCount = 1;
            }
        }

        if (startParenthesisCount > 1) {
            return QString();
        }

        const auto endChar = (startChar == s_leftParenthesisChar ? s_rightParenthesisChar : startChar);

        QString title;
        ReverseSolidusHandler reverseSolidus;

        while (line.position() < line.length()) {
            reverseSolidus.process(line.currentChar());

            if (line.currentChar() == endChar && !reverseSolidus.isPrevReverseSolidus()) {
                endStarted = true;

                --startParenthesisCount;
                line.nextChar();

                if (!isEmptyLine(line)) {
                    return QString();
                }

                break;
            } else if (line.currentChar() == startChar && !reverseSolidus.isPrevReverseSolidus()) {
                return QString();
            } else {
                title.append(line.currentChar());
            }

            line.nextChar();
            reverseSolidus.next();
        }

        return title;
    }

    return QString();
}

bool isHorizontalLine(Line &s)
{
    if (s.length() < 3) {
        return false;
    }

    QChar c;

    if (s.currentChar() == s_asteriskChar) {
        c = s_asteriskChar;
    } else if (s.currentChar() == s_minusChar) {
        c = s_minusChar;
    } else if (s.currentChar() == s_lowLineChar) {
        c = s_lowLineChar;
    } else {
        return false;
    }

    int count = 1;

    const auto st = s.currentState();
    s.nextChar();

    while (s.position() < s.length()) {
        if (s.currentChar() != c && !s.currentChar().isSpace()) {
            break;
        } else if (s.currentChar() == c) {
            ++count;
        }

        s.nextChar();
    }

    const auto pos = s.position();

    s.restoreState(&st);

    if (count < 3) {
        return false;
    }

    if (pos == s.length()) {
        return true;
    }

    return false;
}

QString readEscapedSequence(Line &line,
                            qsizetype *endPos)
{
    const auto start = line.position();

    if (start >= line.length()) {
        return {};
    }

    ReverseSolidusHandler reverseSolidus;

    while (line.position() < line.length()) {
        if (reverseSolidus.isNotEscaped(line.currentChar()) && line.currentChar().isSpace()) {
            break;
        }

        line.nextChar();
        reverseSolidus.next();
    }

    if (endPos) {
        *endPos = line.position() - 1;
    }

    return line.slicedCopy(start, line.position() - start);
}

bool operator<(const StyleDelim &s1,
               const StyleDelim &s2)
{
    if (s1.startLine() != -1 && s2.startLine() == -1) {
        return true;
    } else if (s1.startLine() == -1 && s2.startLine() != -1) {
        return false;
    } else {
        return (s1.startLine() < s2.startLine()
                || (s1.startLine() == s2.startLine() && s1.startColumn() < s2.startColumn()));
    }
}

bool isEmail(const QString &url)
{
    static const auto isAllowed = [](const QChar &ch) -> bool {
        const auto unicode = ch.unicode();
        return (
            (unicode >= 48 && unicode <= 57) || (unicode >= 97 && unicode <= 122) || (unicode >= 65 && unicode <= 90));
    };

    static const auto isAdditional = [](const QChar &ch) -> bool {
        const auto unicode = ch.unicode();
        return (unicode == 33
                || (unicode >= 35 && unicode <= 39)
                || unicode == 42
                || unicode == 43
                || (unicode >= 45 && unicode <= 47)
                || unicode == 61
                || unicode == 63
                || (unicode >= 94 && unicode <= 96)
                || (unicode >= 123 && unicode <= 126));
    };

    auto i = (url.startsWith(s_mailtoString) ? 7 : 0);
    const auto dogPos = url.indexOf(s_commercialAtChar, i);

    if (dogPos != -1) {
        if (i == dogPos) {
            return false;
        }

        i = skipIf(
            i,
            url,
            [](const QChar &ch) {
                return (isAllowed(ch) || isAdditional(ch));
            },
            dogPos);

        if (i != dogPos) {
            return false;
        }

        auto checkToDot = [&](qsizetype start, qsizetype dotPos) -> bool {
            static const qsizetype maxlen = 63;

            if (dotPos - start > maxlen || start + 1 > dotPos || start >= url.length() || dotPos > url.length()) {
                return false;
            }

            if (url[start] == s_minusChar) {
                return false;
            }

            if (url[dotPos - 1] == s_minusChar) {
                return false;
            }

            start = skipIf(
                start,
                url,
                [](const QChar &ch) {
                    return (isAllowed(ch) || ch == s_minusChar);
                },
                dotPos);

            if (start != dotPos) {
                return false;
            }

            return true;
        };

        qsizetype dotPos = url.indexOf(s_dotChar, dogPos + 1);

        if (dotPos != -1) {
            i = dogPos + 1;

            while (dotPos != -1) {
                if (!checkToDot(i, dotPos)) {
                    return false;
                }

                i = dotPos + 1;
                dotPos = url.indexOf(s_dotChar, i);
            }

            if (!checkToDot(i, url.length())) {
                return false;
            }

            return true;
        }
    }

    return false;
}

bool isCommonMarkAutolinkUri(const QString &uri)
{
    static const QString s_allowedInSchema = QStringLiteral("+.-");

    const auto i = uri.indexOf(s_colonChar);

    if (i > 1 && i <= 32 && isAsciiLetter(uri[0])) {
        for (auto j = 1; j < i; ++j) {
            if (!isAsciiLetter(uri[j]) && !uri[j].isDigit() && s_allowedInSchema.indexOf(uri[j]) == -1) {
                return false;
            }
        }

        for (auto j = i + i; j < uri.length(); ++j) {
            if (isAsciiControl(uri[j])
                || uri[j] == s_spaceChar
                || uri[j] == s_lessSignChar
                || uri[j] == s_greaterSignChar) {
                return false;
            }
        }

        return true;
    }

    return false;
}

bool isValidTagName(const QString &tag)
{
    if (!tag.isEmpty()) {
        if (isAsciiLetter(tag[0])) {
            for (qsizetype i = 1; i < tag.length(); ++i) {
                if (!(isAsciiLetter(tag[i]) || tag[i].isDigit() || tag[i] == s_minusChar)) {
                    return false;
                }
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

bool readHtmlAttrName(Line &line)
{
    if (isAsciiLetter(line.currentChar()) || line.currentChar() == s_lowLineChar || line.currentChar() == s_colonChar) {
        line.nextChar();

        while (line.position() < line.length()
               && (isAsciiLetter(line.currentChar())
                   || line.currentChar().isDigit()
                   || line.currentChar() == s_lowLineChar
                   || line.currentChar() == s_colonChar
                   || line.currentChar() == s_dotChar
                   || line.currentChar() == s_minusChar)) {
            line.nextChar();
        }

        return true;
    }

    return false;
}

bool readHtmlAttrValue(Line &line)
{
    if (line.currentChar() == s_quotationMarkChar) {
        line.nextChar();
        skipIf(line, [](const QChar &c) {
            return c != s_quotationMarkChar;
        });

        const auto res = (line.currentChar() == s_quotationMarkChar);

        if (res) {
            line.nextChar();
        }

        return res;
    } else if (line.currentChar() == s_apostropheChar) {
        line.nextChar();
        skipIf(line, [](const QChar &c) {
            return c != s_apostropheChar;
        });

        const auto res = (line.currentChar() == s_apostropheChar);

        if (res) {
            line.nextChar();
        }

        return res;
    } else {
        const auto pos = line.position();
        skipIf(line, [](const QChar &c) {
            return (!c.isSpace()
                    && c != s_quotationMarkChar
                    && c != s_apostropheChar
                    && c != s_equalSignChar
                    && c != s_lessSignChar
                    && c != s_greaterSignChar
                    && c != s_graveAccentChar);
        });

        return line.position() > pos;
    }
}

bool readHtmlAttributes(Line &line)
{
    qsizetype p = line.position();

    skipSpaces(line);

    while (line.position() < line.length() && line.currentChar() != s_greaterSignChar) {
        skipSpaces(line);

        if (p == line.position()) {
            return false;
        }

        if (line.currentChar() == s_solidusChar) {
            return true;
        }

        if (line.position() < line.length() && !readHtmlAttrName(line)) {
            return false;
        }

        p = line.position();

        skipSpaces(line);

        if (line.currentChar() != s_equalSignChar) {
            if (line.currentChar() == s_solidusChar || line.currentChar() == s_greaterSignChar) {
                return true;
            } else if (p == line.position()) {
                return false;
            } else {
                continue;
            }
        }

        line.nextChar();

        skipSpaces(line);

        if (!readHtmlAttrValue(line)) {
            return false;
        }

        p = line.position();
    }

    return true;
}

bool isHtmlTag(const QString &tag,
               Line &line,
               bool closed)
{
    if (!isValidTagName(tag)) {
        return false;
    }

    if (closed) {
        return true;
    }

    if (!readHtmlAttributes(line)) {
        return false;
    }

    if (line.currentChar() == s_solidusChar) {
        line.nextChar();
    }

    return (line.currentChar() == s_greaterSignChar);
}

const QSet<QString> s_rule1 = {QStringLiteral("pre"),
                               QStringLiteral("script"),
                               QStringLiteral("style"),
                               QStringLiteral("textarea")};

int htmlTagRule(Line &line)
{
    static const QSet<QString> s_rule6 = {
        QStringLiteral("address"),  QStringLiteral("article"),    QStringLiteral("aside"),
        QStringLiteral("base"),     QStringLiteral("basefont"),   QStringLiteral("blockquote"),
        QStringLiteral("body"),     QStringLiteral("caption"),    QStringLiteral("center"),
        QStringLiteral("col"),      QStringLiteral("colgroup"),   QStringLiteral("dd"),
        QStringLiteral("details"),  QStringLiteral("dialog"),     QStringLiteral("dir"),
        QStringLiteral("div"),      QStringLiteral("dl"),         QStringLiteral("dt"),
        QStringLiteral("fieldset"), QStringLiteral("figcaption"), QStringLiteral("figure"),
        QStringLiteral("footer"),   QStringLiteral("form"),       QStringLiteral("frame"),
        QStringLiteral("frameset"), QStringLiteral("h1"),         QStringLiteral("h2"),
        QStringLiteral("h3"),       QStringLiteral("h4"),         QStringLiteral("h5"),
        QStringLiteral("h6"),       QStringLiteral("head"),       QStringLiteral("header"),
        QStringLiteral("hr"),       QStringLiteral("html"),       QStringLiteral("iframe"),
        QStringLiteral("legend"),   QStringLiteral("li"),         QStringLiteral("link"),
        QStringLiteral("main"),     QStringLiteral("menu"),       QStringLiteral("menuitem"),
        QStringLiteral("nav"),      QStringLiteral("noframes"),   QStringLiteral("ol"),
        QStringLiteral("optgroup"), QStringLiteral("option"),     QStringLiteral("p"),
        QStringLiteral("param"),    QStringLiteral("section"),    QStringLiteral("search"),
        QStringLiteral("summary"),  QStringLiteral("table"),      QStringLiteral("tbody"),
        QStringLiteral("td"),       QStringLiteral("tfoot"),      QStringLiteral("th"),
        QStringLiteral("thead"),    QStringLiteral("title"),      QStringLiteral("tr"),
        QStringLiteral("track"),    QStringLiteral("ul")};

    const auto st = line.currentState();

    if (line.currentChar() == s_lessSignChar) {
        line.nextChar();

        QString tag;

        while (
            line.position() < line.length() && !line.currentChar().isSpace() && line.currentChar() != s_lessSignChar) {
            tag.append(line.currentChar());

            if (line.currentChar() == s_greaterSignChar) {
                break;
            }

            line.nextChar();
        }

        if (!tag.isEmpty()) {
            tag = tag.toLower();

            bool closed = false;

            if (tag.endsWith(s_greaterSignChar)) {
                closed = true;
                tag.removeLast();
            }

            if (s_rule1.contains(tag)) {
                return 1;
            } else if (tag.startsWith(s_commentStartString)) {
                return 2;
            } else if (tag.startsWith(s_questionMarkChar)) {
                return 3;
            } else if (tag.length() > 1
                       && tag[0] == s_exclamationMarkChar
                       && ((tag[1].unicode() >= 65 && tag[1].unicode() <= 90)
                           || (tag[1].unicode() >= 97 && tag[1].unicode() <= 122))) {
                return 4;
            } else if (tag.startsWith(s_cdataStartString)) {
                return 5;
            } else {
                if (tag.startsWith(s_solidusChar)) {
                    skipSpaces(line);

                    if (line.currentChar() != s_greaterSignChar) {
                        line.restoreState(&st);

                        return -1;
                    }

                    tag.removeFirst();
                }

                if (tag.endsWith(s_solidusChar)) {
                    if (line.currentChar() != s_greaterSignChar) {
                        line.restoreState(&st);

                        return -1;
                    }

                    tag.removeLast();
                }

                if (s_rule6.contains(tag)) {
                    return 6;
                } else if (isHtmlTag(tag, line, closed)) {
                    skipSpaces(line);

                    return 7;
                }
            }
        }
    }

    line.restoreState(&st);

    return -1;
}

bool isClosed(Line &line,
              int rule,
              bool onSameLine)
{
    switch (rule) {
    case 1: {
        while (line.position() < line.length()) {
            skipIf(line, [](const QChar &c) {
                return c != s_lessSignChar;
            });

            if (line.currentChar() != s_lessSignChar) {
                return false;
            }

            line.nextChar();

            if (line.currentChar() == s_solidusChar) {
                line.nextChar();

                QString tag;

                while (line.position() < line.length() && line.currentChar() != s_greaterSignChar) {
                    tag.append(line.currentChar());
                    line.nextChar();
                }

                tag = tag.toLower();

                if (s_rule1.contains(tag) && line.currentChar() == s_greaterSignChar) {
                    return true;
                }
            }
        }

        return false;
    } break;

    case 2: {
        if (onSameLine && line.currentChar() == s_greaterSignChar) {
            return true;
        }

        skipIf(line, [](const QChar &c) {
            return c != s_minusChar;
        });

        qsizetype count = 0;

        while (line.position() < line.length()) {
            if (line.currentChar() == s_minusChar) {
                ++count;
            } else if (line.currentChar() == s_greaterSignChar) {
                if (count > 1) {
                    return true;
                }

                count = 0;
            } else {
                count = 0;
            }

            line.nextChar();
        }

        return false;
    } break;

    case 3: {
        while (line.position() < line.length()) {
            if (line.currentChar() == s_questionMarkChar) {
                line.nextChar();

                if (line.currentChar() == s_greaterSignChar) {
                    return true;
                } else {
                    continue;
                }
            }

            line.nextChar();
        }

        return false;
    } break;

    case 4: {
        while (line.position() < line.length()) {
            if (line.currentChar() == s_greaterSignChar) {
                return true;
            }

            line.nextChar();
        }

        return false;
    } break;

    case 5: {
        qsizetype count = 0;

        while (line.position() < line.length()) {
            if (line.currentChar() == s_rightSquareBracketChar) {
                ++count;
            } else if (line.currentChar() == s_greaterSignChar) {
                if (count == 2) {
                    return true;
                }

                count = 0;
            } else {
                count = 0;
            }

            line.nextChar();
        }

        return false;
    } break;

    case 6:
    case 7: {
        return (onSameLine ? false : isEmptyLine(line));
    } break;

    default:
        return false;
    }
}

} /* namespace MD */
