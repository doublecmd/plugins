/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "html_parser.h"
#include "constants.h"
#include "paragraph_parser.h"
#include "utils.h"

// Qt include.
#include <QSet>

namespace MD
{

HTMLParser::HTMLParser(Parser *parser)
    : BlockParser(parser)
{
}

HTMLParser::~HTMLParser() = default;

BlockState HTMLParser::check(Line &currentLine,
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
        m_rule = htmlTagRule(currentLine);

        if (m_rule == 7
            && ctx.parent()->children().back().block()
            && dynamic_cast<ParagraphParser *>(ctx.parent()->children().back().block())) {
            currentLine.restoreState();

            return BlockState::None;
        }

        if (m_rule != -1) {
            const auto closed = isClosed(currentLine, m_rule, true);

            currentLine.nextChar();

            if (!(m_rule == 7 && !isEmptyLine(currentLine))) {
                if (checkWithoutProcessing) {
                    currentLine.restoreState();
                } else {
                    ctx.setNotFinished(true);

                    currentLine.skip();
                }

                if (closed) {
                    return BlockState::Stop;
                } else {
                    return BlockState::Continue;
                }
            }
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState HTMLParser::continueCheck(Line &currentLine,
                                     TextStream &,
                                     QSharedPointer<Document>,
                                     Context &ctx,
                                     const QString &,
                                     const QString &)
{
    currentLine.saveState();

    if (isClosed(currentLine, m_rule, false)) {
        currentLine.skip();

        ctx.setNotFinished(false);

        return BlockState::Stop;
    } else {
        currentLine.skip();

        return BlockState::Continue;
    }
}

BlockState HTMLParser::process(Line &currentLine,
                               TextStream &,
                               QSharedPointer<Document>,
                               QSharedPointer<Block> parent,
                               Context &ctx,
                               const QString &,
                               const QString &,
                               QStringList &)
{
    if (!m_html) {
        if (isEmptyLine(currentLine)) {
            currentLine.skip();

            return BlockState::Continue;
        }

        m_html = QSharedPointer<RawHtml>::create();
        m_html->setStartColumn(currentLine.position());
        m_html->setStartLine(currentLine.lineNumber());
        m_html->setEndColumn(currentLine.length() - 1);
        m_html->setEndLine(currentLine.lineNumber());
        ctx.setItem(m_html.get());

        m_html->setText(currentLine.slicedCopy(currentLine.position()));

        skipSpaces(currentLine);

        m_rule = htmlTagRule(currentLine);

        const auto closed = isClosed(currentLine, m_rule, true);

        parent->appendItem(m_html);

        ctx.setNotFinished(true);

        if (closed) {
            reset(ctx);

            currentLine.skip();

            return BlockState::Stop;
        }
    } else {
        const auto data = currentLine.slicedCopy(currentLine.position());

        if ((m_rule >= 6 && !data.simplified().isEmpty()) || m_rule < 6) {
            m_html->setEndColumn(currentLine.length() ? currentLine.length() - 1 : 0);
            m_html->setEndLine(currentLine.lineNumber());
            m_html->setText(m_html->text() + s_newLineChar + data);
        }

        if (isClosed(currentLine, m_rule, false)) {
            reset(ctx);

            currentLine.skip();

            return BlockState::Stop;
        }
    }

    currentLine.skip();

    return BlockState::Continue;
}

void HTMLParser::reset(Context &ctx)
{
    ctx.setNotFinished(false);

    resetOnAllContexts();
}

void HTMLParser::resetOnAllContexts()
{
    m_rule = -1;
    m_html.reset();
}

bool HTMLParser::mayBreakParagraph(Line &,
                                   TextStream &,
                                   QSharedPointer<Document>,
                                   Context &) const
{
    return true;
}

} /* namespace MD */
