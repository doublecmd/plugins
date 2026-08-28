/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_PARAGRAPH_PARSER_H_INCLUDED
#define MD4QT_MD_PARAGRAPH_PARSER_H_INCLUDED

// md4qt include.
#include "block_parser.h"

namespace MD
{

class InlineContext;

/*!
 * \class MD::ParagraphParser
 * \inmodule md4qt
 * \inheaderfile md4qt/paragraph_parser.h
 *
 * \brief Paragraph parser.
 *
 * Paragraph parser.
 */
class ParagraphParser : public BlockParser
{
public:
    explicit ParagraphParser(Parser *parser);
    ~ParagraphParser() override;

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
     * for work for this kind of block this method should just skip the line.
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
    bool canBeLazyLine(Line &line,
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

    /*!
     * Place text objects in paragraph.
     *
     * \a ctx Inline context.
     *
     * \a stream Stream.
     *
     * \a p Paragraph object.
     *
     * \a toSkip Position for skipping.
     */
    static void makeTextObjects(InlineContext &ctx,
                                ParagraphStream &stream,
                                QSharedPointer<Block> p,
                                const WithPosition &toSkip = {});

    /*!
     * Convert MD::Paragraph to label.
     *
     * \a p Paragraph.
     */
    static QString paragraphToLabel(QSharedPointer<Paragraph> p);

private:
    enum class RefLinkState {
        Wrong,
        Continue,
        Finished
    }; // enum class RefLinkState

    enum class RefLinkParserStage {
        // [
        S0,
        // label
        S1,
        // ]:
        S2,
        // url
        S3,
        // title
        S4,
        SF
    }; // enum class RefLinkParserStage

    RefLinkState checkForReferenceLink(Line &currentLine,
                                       QSharedPointer<Document> doc,
                                       const Context &ctx,
                                       const QString &path,
                                       const QString &fileName);

    void clearRefLink();

    // Returns the last line number of reference link.
    qsizetype insertRefLink(QSharedPointer<Document> doc,
                            qsizetype pStartlineNumber);

private:
    QSharedPointer<Paragraph> m_paragraph;
    QSharedPointer<Link> m_reference;
    qsizetype m_startLine = -1;
    RefLinkParserStage m_refLinkStage = RefLinkParserStage::S0;
    QString m_refLinkLabel;
    QString m_refLinkTitle;
    WithPosition m_refLinkTextPos;
    WithPosition m_refLinkUrlPos;
    WithPosition m_refLinkTitlePos;
    QChar m_refLinkTitleStartChar;
    bool m_wasSpace = false;
    bool m_finished = false;
    int m_refLinkStartParenthesisCount = 0;
    qsizetype m_refLinkTitleStartPos = -1;
}; // class ParagraphParser

} /* namespace MD */

#endif // MD4QT_MD_PARAGRAPH_PARSER_H_INCLUDED
