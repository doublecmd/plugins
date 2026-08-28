/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include
#include "indented_code_parser.h"
#include "constants.h"
#include "utils.h"

namespace MD
{

inline qsizetype codeColumn(qsizetype indent)
{
    return (indentFromColumn(indent) + 4);
}

IndentedCodeParser::IndentedCodeParser(Parser *parser)
    : BlockParser(parser)
{
}

IndentedCodeParser::~IndentedCodeParser() = default;

BlockState IndentedCodeParser::check(Line &currentLine,
                                     TextStream &,
                                     QSharedPointer<Document>,
                                     Context &ctx,
                                     const QString &,
                                     const QString &,
                                     bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (currentLine.column() >= codeColumn(ctx.maxAvailableIndent())) {
        if (checkWithoutProcessing) {
            currentLine.restoreState();
        } else {
            currentLine.skip();
        }

        return BlockState::Continue;
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState IndentedCodeParser::continueCheck(Line &currentLine,
                                             TextStream &,
                                             QSharedPointer<Document>,
                                             Context &ctx,
                                             const QString &,
                                             const QString &)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (currentLine.column() >= codeColumn(ctx.maxAvailableIndent()) || isEmptyLine(currentLine)) {
        currentLine.skip();

        return BlockState::Continue;
    }

    currentLine.restoreState();

    return BlockState::Stop;
}

BlockState IndentedCodeParser::process(Line &currentLine,
                                       TextStream &,
                                       QSharedPointer<Document>,
                                       QSharedPointer<Block> parent,
                                       Context &ctx,
                                       const QString &,
                                       const QString &,
                                       QStringList &)
{
    currentLine.saveState();

    while (currentLine.currentChar().isSpace()
           && currentLine.position() < currentLine.length()
           && currentLine.column() < codeColumn(ctx.maxAvailableIndent())) {
        currentLine.nextChar();
    }

    if (currentLine.column() >= codeColumn(ctx.maxAvailableIndent()) || isEmptyLine(currentLine)) {
        if (!m_code) {
            if (isEmptyLine(currentLine)) {
                currentLine.skip();

                return BlockState::Continue;
            }

            m_code = QSharedPointer<Code>::create(QString(), false, false);
            m_code->setStartColumn(currentLine.position()
                                   - (currentLine.column() > codeColumn(ctx.maxAvailableIndent()) ? 1 : 0));
            m_code->setStartLine(currentLine.lineNumber());
            parent->appendItem(m_code);
            ctx.setItem(m_code.get());
        }

        if (m_code) {
            const QString data = QString(currentLine.column() - codeColumn(ctx.maxAvailableIndent()), s_spaceChar)
                + currentLine.slicedCopy(currentLine.position());

            if (!data.isEmpty()) {
                auto code = m_code->text();

                if (!code.isEmpty()) {
                    code.append(s_newLineChar);
                }

                for (qsizetype i = 0; i < m_emptyLinesCount; ++i) {
                    code.append(s_newLineChar);
                }

                code.append(data);

                m_code->setText(code);

                m_emptyLinesCount = 0;

                m_code->setEndColumn(currentLine.length() - 1);
                m_code->setEndLine(currentLine.lineNumber());
            } else {
                ++m_emptyLinesCount;
            }
        }

        currentLine.skip();

        return BlockState::Continue;
    } else {
        currentLine.restoreState();

        reset(ctx);

        return BlockState::Stop;
    }
}

void IndentedCodeParser::reset(Context &)
{
    resetOnAllContexts();
}

void IndentedCodeParser::resetOnAllContexts()
{
    m_emptyLinesCount = 0;
    m_code.reset();
}

bool IndentedCodeParser::mayBreakParagraph(Line &,
                                           TextStream &,
                                           QSharedPointer<Document>,
                                           Context &) const
{
    return false;
}

bool IndentedCodeParser::canBeLazyLine(Line &,
                                       TextStream &,
                                       QSharedPointer<Document>,
                                       Context &) const
{
    return true;
}

} /* namespace MD */
