/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_BLOCK_PARSER_H_INCLUDED
#define MD4QT_MD_BLOCK_PARSER_H_INCLUDED

// md4qt include.
#include "context.h"
#include "doc.h"
#include "text_stream.h"

namespace MD
{

class Parser;

/*!
 * \enum MD::BlockState
 * \inmodule md4qt
 * \inheaderfile md4qt/block_parser.h
 *
 * \brief Enumeration of block parsing states.
 *
 * \value None Means this instance does not handle current line.
 * \value Stop Means block was finished and next content should go for parsing of other block.
 * \value Continue Means this instance handles this line and parsing should give next lines to this block parser.
 * \value ContinueWithoutAppendingChildCtx Means this instance handles this line and parsing should give next lines
 *        to this block parser but child context should not be added nor updated.
 * \value Discard Used on the next lines after check method to break processing with this block and try to process
 *        with other blocks.
 */
enum class BlockState {
    None = 0,
    Stop,
    Continue,
    ContinueWithoutAppendingChildCtx,
    Discard
}; // enum class BlockState

/*!
 * \class MD::BlockParser
 * \inmodule md4qt
 * \inheaderfile md4qt/block_parser.h
 *
 * \brief Base class for parsing a block.
 *
 * Instances of this class invoke during parsing process for checking specific
 * rules to handle this or that type of a block.
 */
class BlockParser
{
protected:
    explicit BlockParser(Parser *parser);

public:
    virtual ~BlockParser();

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
    virtual BlockState check(Line &currentLine,
                             TextStream &stream,
                             QSharedPointer<Document> doc,
                             Context &ctx,
                             const QString &path,
                             const QString &fileName,
                             bool checkWithoutProcessing = false) = 0;

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
    virtual BlockState continueCheck(Line &currentLine,
                                     TextStream &stream,
                                     QSharedPointer<Document> doc,
                                     Context &ctx,
                                     const QString &path,
                                     const QString &fileName) = 0;

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
    virtual BlockState process(Line &currentLine,
                               TextStream &stream,
                               QSharedPointer<Document> doc,
                               QSharedPointer<Block> parent,
                               Context &ctx,
                               const QString &path,
                               const QString &fileName,
                               QStringList &linksToParse);

    /*!
     * Reset state of the block parser for further reuse on the given context.
     *
     * \a ctx Context.
     */
    virtual void reset(Context &ctx) = 0;

    /*!
     * Reset state of the block parser for further reuse.
     */
    virtual void resetOnAllContexts() = 0;

    /*!
     * Returns pointer to parser.
     */
    Parser *parser() const;

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
    virtual bool mayBreakParagraph(Line &line,
                                   TextStream &stream,
                                   QSharedPointer<Document> doc,
                                   Context &ctx) const = 0;

    /*!
     * Returns whether this kind of block can be a lazy line.
     *
     * \a line Line.
     *
     * \a stream Stream.
     *
     * \a doc Document.
     *
     * \a ctx Context.
     */
    virtual bool canBeLazyLine(Line &line,
                               TextStream &stream,
                               QSharedPointer<Document> doc,
                               Context &ctx) const;

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
    virtual void finish(Line &currentLine,
                        TextStream &stream,
                        QSharedPointer<Document> doc,
                        QSharedPointer<Block> parent,
                        Context &ctx,
                        const QString &path,
                        const QString &fileName,
                        QStringList &linksToParse);

    /*!
     * Returns current block.
     *
     * \a ctx Context.
     */
    virtual QSharedPointer<Block> currentBlock(const Context &ctx) const;

    /*!
     * Returns whether not finished block will be discarded.
     */
    virtual bool isNotFinishedDiscardable() const;

    /*!
     * Returns whether the given line is a paragraph continuation.
     *
     * \a line Line.
     *
     * \a stream Stream.
     *
     * \a doc Document.
     *
     * \a ctx Context.
     *
     * \a considerIndents Whether indents should be considered?
     */
    bool isLazyContinuationLine(Line &line,
                                TextStream &stream,
                                QSharedPointer<Document> doc,
                                Context &ctx,
                                bool considerIndents) const;

protected:
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
    virtual BlockState processOneTime(Line &currentLine,
                                      TextStream &stream,
                                      QSharedPointer<Document> doc,
                                      QSharedPointer<Block> parent,
                                      Context &ctx,
                                      const QString &path,
                                      const QString &fileName,
                                      QStringList &linksToParse);

private:
    Parser *m_parser;
}; // class BlockParser

} /* namespace MD */

#endif // MD4QT_MD_BLOCK_PARSER_H_INCLUDED
