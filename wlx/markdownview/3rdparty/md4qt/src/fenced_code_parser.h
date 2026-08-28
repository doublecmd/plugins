/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_FENCED_CODE_PARSER_H_INCLUDED
#define MD4QT_MD_FENCED_CODE_PARSER_H_INCLUDED

// md4qt include
#include "block_parser.h"

namespace MD
{

/*!
 * \class MD::FencedCodeParser
 * \inmodule md4qt
 * \inheaderfile md4qt/fenced_code_parser.h
 *
 * \brief Fenced code parser.
 *
 * Fenced code parser.
 */
class FencedCodeParser : public BlockParser
{
public:
    explicit FencedCodeParser(Parser *parser);
    virtual ~FencedCodeParser();

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
     * Reset state of the block parser for further reuse.
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

    /*!
     * Finish things. This method invokes after MD::BlockState::Stop state recieved or at the end of file.
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
    void finish(Line &currentLine,
                TextStream &stream,
                QSharedPointer<Document> doc,
                QSharedPointer<Block> parent,
                Context &ctx,
                const QString &path,
                const QString &fileName,
                QStringList &linksToParse) override;

private:
    WithPosition m_startDelim;
    QChar m_startChar;
    QSharedPointer<Code> m_code;
    QSharedPointer<Paragraph> m_mathParagraph;
    QSharedPointer<Math> m_math;
    qsizetype m_emptyColumn;
    qsizetype m_emptyColumnLine;
    qsizetype m_openCount = 0;
    qsizetype m_emptyLinesCount = 0;
    qsizetype m_startColumn = 0;
}; // class FencedCodeParser

} /* namespace MD */

#endif // MD4QT_MD_FENCED_CODE_PARSER_H_INCLUDED
