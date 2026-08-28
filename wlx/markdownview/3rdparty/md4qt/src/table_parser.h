/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_TABLE_PARSER_H_INCLUDED
#define MD4QT_MD_TABLE_PARSER_H_INCLUDED

// md4qt include.
#include "block_parser.h"
#include "reverse_solidus.h"

namespace MD
{

/*!
 * \class MD::TableParser
 * \inmodule md4qt
 * \inheaderfile md4qt/table_parser.h
 *
 * \brief Table parser.
 *
 * Table parser
 */
class TableParser : public BlockParser
{
public:
    explicit TableParser(Parser *parser);
    ~TableParser() override;

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
     * Returns whether not finished block will be discarded.
     */
    bool isNotFinishedDiscardable() const override;

private:
    void processCell(Line line,
                     QSharedPointer<Document> doc,
                     const QString &path,
                     const QString &fileName,
                     QStringList &linksToParse);
    void processAlignment(Line line);

    template<class Func>
    void processRow(Line &currentLine,
                    Func func,
                    bool addRow)
    {
        m_table->setEndColumn(currentLine.length() - 1);
        m_table->setEndLine(currentLine.lineNumber());
        const auto startPos = currentLine.position();

        if (currentLine.currentChar() == s_verticalLineChar) {
            currentLine.nextChar();
            skipSpaces(currentLine);
        }

        if (addRow) {
            m_row = QSharedPointer<TableRow>::create();
            m_row->setStartColumn(startPos);
            m_row->setStartLine(currentLine.lineNumber());
            m_row->setEndColumn(currentLine.length() - 1);
            m_row->setEndLine(currentLine.lineNumber());
            m_table->appendRow(m_row);
        }

        for (int i = 0; i < m_columnsCount; ++i) {
            if (currentLine.position() < currentLine.length()) {
                const auto st = currentLine.currentState();

                ReverseSolidusHandler rs;
                qsizetype spaces = 0;

                while (currentLine.position() < currentLine.length()) {
                    if (rs.isNotEscaped(currentLine.currentChar())) {
                        if (currentLine.currentChar() == s_verticalLineChar) {
                            break;
                        } else if (currentLine.currentChar().isSpace()) {
                            ++spaces;
                        } else {
                            spaces = 0;
                        }
                    } else {
                        spaces = 0;
                    }

                    rs.next();
                    currentLine.nextChar();
                }

                auto line = currentLine.sliced(0, currentLine.position() - spaces);
                line.restoreState(&st);

                if (currentLine.currentChar() == s_verticalLineChar) {
                    currentLine.nextChar();
                }

                func(line);
            }
        }
    }

    void processAlignments(Line &currentLine);

private:
    QSharedPointer<Table> m_table;
    QSharedPointer<TableRow> m_row;
    bool m_checkAlign = false;
    int m_columnsCount = 0;
}; // class TableParser

} /* namespace MD */

#endif // MD4QT_MD_TABLE_PARSER_H_INCLUDED
