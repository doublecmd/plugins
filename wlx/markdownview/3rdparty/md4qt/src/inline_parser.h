/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_INLINE_PARSER_H_INCLUDED
#define MD4QT_MD_INLINE_PARSER_H_INCLUDED

// md4qt include.
#include "doc.h"

namespace MD
{

class Line;
class InlineContext;
class ParagraphStream;
class Parser;
class ReverseSolidusHandler;

/*!
 * \class MD::InlineParser
 * \inmodule md4qt
 * \inheaderfile md4qt/inline_parser.h
 *
 * \brief Base class for parsing inlines.
 *
 * Instances of this class invoke during parsing process for checking specific
 * rules to handle this or that type of an inline.
 */
class InlineParser
{
protected:
    InlineParser();

public:
    virtual ~InlineParser();

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
     * \a stream Text stream.
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
    virtual bool check(Line &line,
                       ParagraphStream &stream,
                       InlineContext &ctx,
                       QSharedPointer<Document> doc,
                       const QString &path,
                       const QString &fileName,
                       QStringList &linksToParse,
                       Parser &parser,
                       const ReverseSolidusHandler &rs) = 0;

    /*!
     * Push state of the parser on stack. For some parsers this is needed, as links, for example,
     * parses its description part as paragraph. This is a subject for implementation of parser...
     */
    virtual void pushState();

    /*!
     * Pop state of the parser from stack.
     *
     * \sa pushState()
     */
    virtual void popState();

    /*!
     * Returns string with all symbols that can start this kind of inline.
     */
    virtual QString startDelimiterSymbols() const = 0;
}; // class InlineParser

} /* namespace MD */

#endif // MD4QT_MD_INLINE_PARSER_H_INCLUDED
