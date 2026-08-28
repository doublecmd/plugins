/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_EMPHASIS_PARSER_H_INCLUDED
#define MD4QT_MD_EMPHASIS_PARSER_H_INCLUDED

// md4qt include.
#include "inline_parser.h"

// Qt include.
#include <QString>

namespace MD
{

class InlineContext;

/*!
 * \class MD::EmphasisParser
 * \inmodule md4qt
 * \inheaderfile md4qt/emphasis_parser.h
 *
 * \brief Emphasis parser.
 *
 * Emphasis parser. May be reused for any kind of emphasis parsing.
 */
class EmphasisParser : public InlineParser
{
protected:
    EmphasisParser();

public:
    ~EmphasisParser() override;

    /*!
     * Check if current character (string) is handled by this parser.
     *
     * Returns true if handled, false otherwise.
     *
     * This method should do a work with context and handle his delimiters
     * and cache in context ready for use object that will be placed into MD::Paragraph.
     *
     * \a line Current line.
     *
     * \a stream Stream.
     *
     * \a ctx Context.
     *
     * \a doc Document.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     *
     * \a linksToParse List of links for further parsing.
     *
     * \a parser Parser.
     *
     * \a rs Auxiliary struct for checking if first charcter is escaped.
     */
    bool check(Line &line,
               ParagraphStream &stream,
               InlineContext &ctx,
               QSharedPointer<Document> doc,
               const QString &path,
               const QString &fileName,
               QStringList &linksToParse,
               Parser &parser,
               const ReverseSolidusHandler &rs) override;

    /*!
     * Returns delimiter character for this kind of emphasis.
     */
    virtual const QChar &symbol() const = 0;

    /*!
     * Returns whether the given sequence of delimiters is a left flanking.
     *
     * \a before Symbol before the sequence, or null character if it's a beginning of a line.
     *
     * \a after Symbol after the sequence, or null character if it's an end of a line.
     */
    virtual bool isLeftFlanking(const QChar &before,
                                const QChar &after) const;

    /*!
     * Returns whether the given sequence of delimiters is a right flanking.
     *
     * \a before Symbol before the sequence, or null character if it's a beginning of a line.
     *
     * \a after Symbol after the sequence, or null character if it's an end of a line.
     */
    virtual bool isRightFlanking(const QChar &before,
                                 const QChar &after) const;

    /*!
     * Returns whether the sequence of the given length is an emphasis.
     *
     * \a length Length of the sequence.
     */
    virtual bool isEmphasis(int length) const = 0;

    /*!
     * Returns whether length of opening and closing delimiter should be equal to each other, or may differ.
     */
    virtual bool isLengthCorrespond() const = 0;

    /*!
     * Calculate all emphasises in context dropping that the text.
     *
     * \a ctx Inline context.
     */
    static void processEmphasises(InlineContext &ctx);

    /*!
     * Returns opening text option (style) for the given delimiter with the given length.
     *
     * \a startPos Start position of the first delimiter.
     *
     * \a lineNumber Line number.
     *
     * \a length Length of the delimiter.
     */
    virtual ItemWithOpts::Styles openStyles(qsizetype startPos,
                                            qsizetype lineNumber,
                                            qsizetype length) const = 0;

    /*!
     * Returns closing text option (style) for the given delimiter with the given length.
     *
     * \a startPos Start position of the first delimiter.
     *
     * \a lineNumber Line number.
     *
     * \a length Length of the delimiter.
     */
    virtual ItemWithOpts::Styles closeStyles(qsizetype startPos,
                                             qsizetype lineNumber,
                                             qsizetype length) const = 0;

    /*!
     * Returns string with all symbols that can start this kind of inline.
     */
    QString startDelimiterSymbols() const override;

protected:
    /*!
     * Returns whether Unicode whitespace?
     *
     * \a ch Character to check.
     */
    bool isUnicodeWhitespace(const QChar &ch) const
    {
        const auto c = ch.unicode();

        if (ch.category() == QChar::Separator_Space) {
            return true;
        } else if (c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D) {
            return true;
        } else {
            return false;
        }
    }
}; // class EmphasisParser

} /* namespace MD */

#endif // MD4QT_MD_EMPHASIS_PARSER_H_INCLUDED
