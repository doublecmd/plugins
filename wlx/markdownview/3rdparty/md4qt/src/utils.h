/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_UTILS_H_INCLUDED
#define MD4QT_MD_UTILS_H_INCLUDED

// md4qt include.
#include "doc.h"
#include "text_stream.h"

// Qt include.
#include <QUrl>

namespace MD
{

class Context;
class StyleDelim;

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Skip characters by the given predicate.
 *
 * \a line String.
 *
 * \a pred Predicate.
 */
template<class Pred>
inline void skipIf(Line &line,
                   Pred pred)
{
    while (line.position() < line.length() && pred(line.currentChar())) {
        line.nextChar();
    }
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns first position of character not satisfying to predicate.
 *
 * \a startPos Start position.
 *
 * \a line String.
 *
 * \a pred Predicate.
 *
 * \a endPos End position. -1 means last character in the string.
 *
 * \sa MD::skipIfBackward
 */
template<class Pred>
inline qsizetype skipIf(qsizetype startPos,
                        const QString &line,
                        Pred pred,
                        qsizetype endPos = -1)
{
    endPos = (endPos < line.length() && endPos > -1 ? endPos : line.length());

    while (startPos < endPos && pred(line[startPos])) {
        ++startPos;
    }

    return startPos;
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns first position of character not satisfying to predicate moving in backward order.
 *
 * \a startPos Start position.
 *
 * \a line String.
 *
 * \a pred Predicate.
 *
 * \a endPos End position. -1 means first character in the string. End position is inclusive.
 *
 * \sa MD::skipIf
 */
template<class Pred>
inline qsizetype skipIfBackward(qsizetype startPos,
                                const QString &line,
                                Pred pred,
                                qsizetype endPos = -1)
{
    const auto lastPos = line.length() - 1;

    if (startPos > lastPos) {
        startPos = lastPos;
    }

    if (startPos < 0) {
        return -1;
    }

    endPos = (endPos >= 0 && endPos <= lastPos && endPos <= startPos ? endPos : 0);

    while (startPos >= endPos && pred(line[startPos])) {
        --startPos;
    }

    return startPos;
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Skip space characters.
 *
 * \a line String.
 */
inline void skipSpaces(Line &line)
{
    skipIf(line, [](const QChar &c) {
        return c.isSpace();
    });
}

/*!
 * Return whether the given line is empty starting at the current position.
 *
 * \a line Line.
 */
inline bool isEmptyLine(Line &line)
{
    const auto st = line.currentState();

    skipSpaces(line);
    const auto empty = (line.position() == line.length());
    line.restoreState(&st);

    return empty;
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Read link's destination.
 *
 * \a line Line
 */
QString readLinkDestination(Line &line);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Remove backslashes from string.
 *
 * \a str String.
 */
void removeBackslashes(QString &str);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Replace entities in the string with corresponding character.
 *
 * \a str String for modifications.
 */
void replaceEntity(QString &str);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Read link's title.
 *
 * \a line Line.
 *
 * \a startChar Receiver for surrounding of title character.
 *
 * \a startParenthesisCount Receiver for count of start parenthesis.
 *
 * \a startPos Receiver for a start position.
 *
 * \a endStarted Receiver for flag means that title is ended.
 */
QString readLinkTitle(Line &line,
                      QChar &startChar,
                      int &startParenthesisCount,
                      qsizetype &startPos,
                      bool &endStarted);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether string a horizontal line?
 *
 * \a s String for checking.
 */
bool isHorizontalLine(Line &s);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Skip escaped sequence of characters till first space.
 *
 * \a line String for reading.
 *
 * \a endPos Receiver of last character's position in sequence.
 */
QString readEscapedSequence(Line &line,
                            qsizetype *endPos = nullptr);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns compare state of two style delimiters by theirs positions.
 *
 * \a s1 Left operand.
 *
 * \a s2 Right operand.
 */
bool operator<(const StyleDelim &s1,
               const StyleDelim &s2);

/*!
 * Returns whether the give object is in the given place.
 *
 * \a where Place where to check if object is in.
 *
 * \a what Object to check.
 */
inline bool isIn(const WithPosition &where,
                 const WithPosition &what)
{
    return (what.startLine() >= where.startLine()
            && what.endLine() <= where.endLine()
            && (what.startLine() == where.startLine() ? what.startColumn() >= where.startColumn() : true)
            && (what.endLine() == where.endLine() ? what.endColumn() <= where.endColumn() : true));
}

/*!
 * Returns whether the give symbol is ASCII control character.
 *
 * \a ch Symbol.
 */
inline bool isAsciiControl(const QChar &ch)
{
    const auto u = ch.unicode();

    return (u <= 0x1F || u == 0x7F);
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the given string a valid URL?
 *
 * \a url String for checking.
 */
inline bool isValidUrl(const QString &url)
{
    const QUrl u(url, QUrl::StrictMode);

    return (u.isValid() && !u.isRelative());
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the given string is an absolute URI according to CommonMark's
 * autolink grammar.
 *
 * \a uri String for checking.
 */
bool isCommonMarkAutolinkUri(const QString &uri);

/*
    "^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?"
    "(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$"
*/
/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the given string a valid email?
 *
 * \a url String for checking.
 */
bool isEmail(const QString &url);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the given character is an ASCII letter.
 *
 * \a c Character.
 */
inline bool isAsciiLetter(const QChar &c)
{
    const auto u = c.unicode();
    return ((u >= 65 && u <= 90) || (u >= 97 && u <= 122));
}

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the given string a valid HTML tag.
 *
 * \a tag String for checking.
 */
bool isValidTagName(const QString &tag);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the HTML attribute name was read.
 *
 * \a line Line.
 */
bool readHtmlAttrName(Line &line);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the HTML attribute value was read.
 *
 * \a line Line.
 */
bool readHtmlAttrValue(Line &line);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the HTML attributes was read.
 *
 * \a line Line.
 */
bool readHtmlAttributes(Line &line);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the HTML tag was read.
 *
 * \a tag HTML tag name.
 *
 * \a line Line.
 *
 * \a closed Flags whether closing ">" was presented.
 */
bool isHtmlTag(const QString &tag,
               Line &line,
               bool closed);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns HTML tag rule.
 *
 * \a line Line.
 */
int htmlTagRule(Line &line);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns whether the HTML tag was closed.
 *
 * \a line Line.
 *
 * \a rule HTML tag rule.
 *
 * \a onSameLine Flags that check is going on the same line as opening "<" character.
 */
bool isClosed(Line &line,
              int rule,
              bool onSameLine);

/*!
 * \inheaderfile md4qt/utils.h
 *
 * Returns indent from column.
 *
 * \a c Column.
 */
inline qsizetype indentFromColumn(int c)
{
    return (c ? c + 1 : c);
}

} /* namespace MD */

#endif // MD4QT_MD_UTILS_H_INCLUDED
