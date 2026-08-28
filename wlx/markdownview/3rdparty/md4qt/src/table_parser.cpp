/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "table_parser.h"
#include "constants.h"
#include "emphasis_parser.h"
#include "inline_context.h"
#include "paragraph_parser.h"
#include "parser.h"
#include "utils.h"

// C++ include.
#include <functional>

namespace MD
{

inline bool isColumnAlignment(Line line)
{
    skipSpaces(line);

    static const QString s_legitime = QStringLiteral(":-");

    if (line.position() == line.length()) {
        return false;
    }

    if (!s_legitime.contains(line.currentChar())) {
        return false;
    }

    if (line.currentChar() == s_colonChar) {
        line.nextChar();
    }

    skipIf(line, [](const QChar &ch) {
        return (ch == s_minusChar);
    });

    if (line.position() == line.length()) {
        return true;
    }

    if (line.currentChar() != s_colonChar && !line.currentChar().isSpace()) {
        return false;
    }

    line.nextChar();

    skipSpaces(line);

    if (line.position() < line.length()) {
        return false;
    }

    return true;
}

inline int isTableAlignment(Line &line)
{
    const auto roll = line.makeRollbackObject();

    skipSpaces(line);

    int c = 0;

    auto wasPipe = false;

    if (line.currentChar() == s_verticalLineChar) {
        wasPipe = true;
        line.nextChar();
        skipSpaces(line);
    }

    auto pos = line.position();

    while (line.position() < line.length()) {
        if (line.currentChar() == s_verticalLineChar) {
            wasPipe = true;

            if (isColumnAlignment(line.sliced(pos, line.position() - pos))) {
                ++c;

                line.nextChar();
                skipSpaces(line);
                pos = line.position();
            } else {
                return 0;
            }
        }

        line.nextChar();
    }

    if (pos < line.length()) {
        if (isColumnAlignment(line.sliced(pos, line.position() - pos))) {
            ++c;
        } else {
            return 0;
        }
    }

    return (wasPipe ? c : 0);
}

inline int isTableHeader(Line &line)
{
    if (line.view().contains(s_verticalLineChar)) {
        const auto roll = line.makeRollbackObject();

        int c = 1;

        skipSpaces(line);

        Line::State st = line.currentState();

        if (line.currentChar() == s_verticalLineChar) {
            line.nextChar();
        }

        ReverseSolidusHandler rs;

        while (line.position() < line.length()) {
            if (rs.isNotEscaped(line.currentChar())) {
                if (line.currentChar() == s_verticalLineChar) {
                    st = line.currentState();
                    ++c;
                }
            }

            rs.next();
            line.nextChar();
        }

        line.restoreState(&st);

        if (line.currentChar() == s_verticalLineChar) {
            line.nextChar();
        }

        return c - (isEmptyLine(line) ? 1 : 0);
    } else {
        return 0;
    }
}

TableParser::TableParser(Parser *parser)
    : BlockParser(parser)
{
}

TableParser::~TableParser() = default;

BlockState TableParser::check(Line &currentLine,
                              TextStream &,
                              QSharedPointer<Document>,
                              Context &ctx,
                              const QString &,
                              const QString &,
                              bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (ctx.isInIndent(currentLine.column())) {
        const auto c = isTableHeader(currentLine);

        if (c) {
            if (checkWithoutProcessing) {
                currentLine.restoreState();
            } else {
                currentLine.skip();

                ctx.setFirstLineNumber(currentLine.lineNumber());
                ctx.setNotFinished(true);

                m_columnsCount = c;
                m_checkAlign = true;
            }

            return BlockState::Continue;
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState TableParser::continueCheck(Line &currentLine,
                                      TextStream &stream,
                                      QSharedPointer<Document> doc,
                                      Context &ctx,
                                      const QString &,
                                      const QString &)
{
    if (ctx.isDiscardForced()) {
        return BlockState::Discard;
    }

    currentLine.saveState();

    skipSpaces(currentLine);

    if (ctx.isInIndent(currentLine.column())) {
        if (m_checkAlign) {
            m_checkAlign = false;

            const auto c = isTableAlignment(currentLine);

            if (m_columnsCount && m_columnsCount == c) {
                currentLine.skip();
                ctx.setNotFinished(false);

                return BlockState::Continue;
            } else {
                return BlockState::Discard;
            }
        } else if (isEmptyLine(currentLine)) {
            return BlockState::Stop;
        } else {
            auto block = parser()->checkBlockExcluding(currentLine, stream, doc, ctx, this);

            if (!dynamic_cast<ParagraphParser *>(block)) {
                return BlockState::Stop;
            } else {
                currentLine.skip();

                return BlockState::Continue;
            }
        }
    }

    currentLine.restoreState();

    if (m_checkAlign) {
        return BlockState::Discard;
    } else {
        return BlockState::Stop;
    }
}

void TableParser::processCell(Line line,
                              QSharedPointer<Document> doc,
                              const QString &path,
                              const QString &fileName,
                              QStringList &linksToParse)
{
    auto cell = QSharedPointer<TableCell>::create();

    skipSpaces(line);

    cell->setStartLine(line.lineNumber());

    if (line.position() < line.length()) {
        cell->setStartColumn(line.position());
    } else {
        cell->setStartColumn(line.length() - 1);
    }

    cell->setEndColumn(line.length() - 1);
    cell->setEndLine(line.lineNumber());

    skipSpaces(line);

    InlineContext inlineContext;

    parser()->pushStateOfInliners();

    ParagraphStream::HashedLines lines;
    lines.insert(line.lineNumber(), line);

    ParagraphStream pStream(lines, line.lineNumber(), line.lineNumber());
    const auto pst = pStream.currentState();

    line = pStream.readLine();

    ReverseSolidusHandler rs;

    while (line.position() < line.length()) {
        auto processed = false;

        rs.process(line.currentChar());

        const auto parsers = parser()->inlineParsersFor(line.currentChar());

        for (const auto &p : parsers) {
            if (p->check(line, pStream, inlineContext, doc, path, fileName, linksToParse, *parser(), rs)) {
                processed = true;
                break;
            }
        }

        if (!processed) {
            rs.next();
            line.nextChar();
        } else {
            rs.clear();
        }
    }

    pStream.restoreState(&pst);

    parser()->popStateOfInliners();

    EmphasisParser::processEmphasises(inlineContext);

    for (const auto &i : std::as_const(inlineContext.inlines())) {
        if (i->type() == ItemType::Code) {
            auto c = static_cast<Code *>(i.get());

            QString data;
            ReverseSolidusHandler rs;

            for (qsizetype i = 0; i < c->text().length(); ++i) {
                if (rs.isEscaped(c->text()[i])) {
                    if (c->text()[i] == s_verticalLineChar) {
                        data.removeLast();
                    }
                }

                data.append(c->text()[i]);

                rs.next();
            }

            c->setText(data);
        }
    }

    ParagraphParser::makeTextObjects(inlineContext, pStream, cell);

    m_row->appendCell(cell);
}

void TableParser::processAlignment(Line line)
{
    bool left = false;
    bool right = false;

    skipSpaces(line);

    if (line.currentChar() == s_colonChar) {
        left = true;
        line.nextChar();
    }

    skipIf(line, [](const QChar &c) {
        return c == s_minusChar;
    });

    if (line.currentChar() == s_colonChar) {
        right = true;
    }

    m_table->setColumnAlignment(
        m_table->columnsCount(),
        ((left && right) ? Table::AlignCenter : (right ? Table::AlignRight : Table::AlignLeft)));
}

void TableParser::processAlignments(Line &currentLine)
{
    processRow(currentLine, std::bind(&TableParser::processAlignment, this, std::placeholders::_1), false);
}

BlockState TableParser::process(Line &currentLine,
                                TextStream &stream,
                                QSharedPointer<Document> doc,
                                QSharedPointer<Block> parent,
                                Context &ctx,
                                const QString &path,
                                const QString &fileName,
                                QStringList &linksToParse)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (!m_table) {
        if (isEmptyLine(currentLine)) {
            currentLine.skip();

            return BlockState::Continue;
        }

        skipSpaces(currentLine);

        m_table = QSharedPointer<Table>::create();
        m_table->setStartColumn(currentLine.position());
        m_table->setStartLine(currentLine.lineNumber());
        m_table->setEndColumn(currentLine.length() - 1);
        m_table->setEndLine(currentLine.lineNumber());
        ctx.setItem(m_table.get());
        parent->appendItem(m_table);

        m_checkAlign = true;

        m_columnsCount = isTableHeader(currentLine);

        processRow(currentLine,
                   std::bind(&TableParser::processCell,
                             this,
                             std::placeholders::_1,
                             doc,
                             path,
                             fileName,
                             std::ref(linksToParse)),
                   true);

        return BlockState::Continue;
    } else if (m_checkAlign) {
        skipSpaces(currentLine);

        m_checkAlign = false;

        processAlignments(currentLine);

        return BlockState::Continue;
    } else {
        if (!isEmptyLine(currentLine) && currentLine.column() - ctx.indentColumnForCheck(false) < 4) {
            auto block = parser()->checkBlockExcluding(currentLine, stream, doc, ctx, this);

            if (!dynamic_cast<ParagraphParser *>(block)) {
                return BlockState::Stop;
            } else {
                processRow(currentLine,
                           std::bind(&TableParser::processCell,
                                     this,
                                     std::placeholders::_1,
                                     doc,
                                     path,
                                     fileName,
                                     std::ref(linksToParse)),
                           true);

                return BlockState::Continue;
            }
        }
    }

    return BlockState::Stop;
}

void TableParser::reset(Context &ctx)
{
    resetOnAllContexts();

    ctx.childIndents().clear();
}

void TableParser::resetOnAllContexts()
{
    m_checkAlign = false;
    m_columnsCount = 0;
    m_table.reset();
    m_row.clear();
}

bool TableParser::mayBreakParagraph(Line &line,
                                    TextStream &,
                                    QSharedPointer<Document>,
                                    Context &) const
{
    return isTableHeader(line) && !isTableAlignment(line);
}

bool TableParser::canBeLazyLine(Line &,
                                TextStream &,
                                QSharedPointer<Document>,
                                Context &) const
{
    return true;
}

bool TableParser::isNotFinishedDiscardable() const
{
    return true;
}

} /* namespace MD */
