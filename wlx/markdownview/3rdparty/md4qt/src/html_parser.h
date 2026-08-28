/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_HTML_PARSER_H_INCLUDED
#define MD4QT_MD_HTML_PARSER_H_INCLUDED

// md4qt include.
#include "block_parser.h"

namespace MD
{

/*!
 * \class MD::HTMLParser
 * \inmodule md4qt
 * \inheaderfile md4qt/html_parser.h
 *
 * \brief HTML block parser.
 *
 * HTML block parser.
 */
class HTMLParser : public BlockParser
{
public:
    explicit HTMLParser(Parser *parser);
    ~HTMLParser() override;

    /*!
     * Returns check state for the current line and block type. This method should return BlockState::None if
     * this string is not handled by him. If this block parser knows how to handle the current line it should
     * return BlockState::Continue or BlockState::Stop.
     *
     * \a currentLine Current processing line.
     *
     * \a stream Stream of the lines.
     *
     * \a doc Current document.
     *
     * \a ctx Current parsing context.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     *
     * \a checkWithoutProcessing Flags to don't do real processing, just check this parser handles this line.
     */
    BlockState check(Line &currentLine,
                     TextStream &stream,
                     QSharedPointer<Document> doc,
                     Context &ctx,
                     const QString &path,
                     const QString &fileName,
                     bool checkWithoutProcessing = false) override;

    /*!
     * Continue check. If checking knows that block is finished it should return BlockState::Stop.
     *
     * \a currentLine Current processing line.
     *
     * \a stream Stream of the lines.
     *
     * \a doc Current document.
     *
     * \a ctx Current parsing context.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     */
    BlockState continueCheck(Line &currentLine,
                             TextStream &stream,
                             QSharedPointer<Document> doc,
                             Context &ctx,
                             const QString &path,
                             const QString &fileName) override;

    /*!
     * Process block - do actual parsing and populating MD::Document. If starting empty line is not a subject
     * for work for this kind of block this method should just skip the line. When parsing of a block is finished
     * this method should return BlockState::Stop.
     *
     * This method should call himself on all children contexts till block finish.
     *
     * \a currentLine Current processing line.
     *
     * \a stream Stream of the lines.
     *
     * \a doc Document.
     *
     * \a parent Parent of this block.
     *
     * \a ctx Current parsing context.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     *
     * \a linksToParse List of links for further parsing.
     */
    BlockState process(Line &currentLine,
                       TextStream &stream,
                       QSharedPointer<Document> doc,
                       QSharedPointer<Block> parent,
                       Context &ctx,
                       const QString &path,
                       const QString &fileName,
                       QStringList &linksToParse) override;

    /*!
     * Reset state of the block parser for further reuse on the given context.
     *
     * \a ctx Context.
     */
    void reset(Context &ctx) override;

    /*!
     * Reset state of the block parser for further reuse.
     */
    void resetOnAllContexts() override;

    /*!
     * Return whether this kind of block may break a paragraph.
     *
     * \a line Line.
     *
     * \a stream Stream.
     *
     * \a doc Document.
     *
     * \a ctx Context.
     */
    bool mayBreakParagraph(Line &line,
                           TextStream &stream,
                           QSharedPointer<Document> doc,
                           Context &ctx) const override;

private:
    int m_rule = -1;
    QSharedPointer<RawHtml> m_html;
}; // class BlockParser

} /* namespace MD */

#endif // MD4QT_MD_HTML_PARSER_H_INCLUDED
