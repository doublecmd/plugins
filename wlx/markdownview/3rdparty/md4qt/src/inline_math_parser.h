/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_INLINE_MATH_PARSER_H_INCLUDED
#define MD4QT_MD_INLINE_MATH_PARSER_H_INCLUDED

// md4qt include.
#include "inline_parser.h"

namespace MD
{

/*!
 * \class MD::InlineMathParser
 * \inmodule md4qt
 * \inheaderfile md4qt/inline_math_parser.h
 *
 * \brief Inline math parser.
 *
 * Inline math parser.
 */
class InlineMathParser : public InlineParser
{
public:
    InlineMathParser();
    ~InlineMathParser() override;

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
     * Returns string with all symbols that can start this kind of inline.
     */
    QString startDelimiterSymbols() const override;
}; // class InlineMathParser

} /* namespace MD */

#endif // MD4QT_MD_INLINE_MATH_PARSER_H_INCLUDED
