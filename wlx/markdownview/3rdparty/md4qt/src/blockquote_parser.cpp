/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "blockquote_parser.h"

namespace MD
{

BlockquoteParser::BlockquoteParser(Parser *parser)
    : BlockParser(parser)
{
}

BlockquoteParser::~BlockquoteParser() = default;

void BlockquoteParser::processGreaterSign(Line &currentLine,
                                          Context &ctx)
{
    auto column = currentLine.column();

    if (currentLine.nextChar().isSpace()) {
        ++column;
        currentLine.nextChar();
    }

    ctx.appendChildIndent(indentFromColumn(column));
}

BlockState BlockquoteParser::check(Line &currentLine,
                                   TextStream &,
                                   QSharedPointer<Document>,
                                   Context &ctx,
                                   const QString &,
                                   const QString &,
                                   bool checkWithoutProcessing)
{
    currentLine.saveState();

    if (isBlockquote(currentLine, ctx)) {
        if (!checkWithoutProcessing) {
            processGreaterSign(currentLine, ctx);
        } else {
            currentLine.restoreState();
        }

        return BlockState::Continue;
    } else {
        currentLine.restoreState();

        return BlockState::None;
    }
}

BlockState BlockquoteParser::continueCheck(Line &currentLine,
                                           TextStream &stream,
                                           QSharedPointer<Document> doc,
                                           Context &ctx,
                                           const QString &,
                                           const QString &)
{
    currentLine.saveState();

    if (isBlockquote(currentLine, ctx)) {
        if (currentLine.currentChar() == s_greaterSignChar) {
            processGreaterSign(currentLine, ctx);
        }

        return BlockState::Continue;
    } else if (isLazyContinuationLine(currentLine, stream, doc, ctx, false)) {
        ctx.appendLazyInfo(currentLine.lineNumber());

        return BlockState::Continue;
    } else {
        auto &nested = ctx.mostNestedChild();

        if (nested.isNotFinished() && nested.block() && nested.block()->isNotFinishedDiscardable()) {
            nested.setDiscardForced(true);
            nested.setNotFinished(false);

            return BlockState::Continue;
        }
    }

    currentLine.restoreState();

    return BlockState::Stop;
}

BlockState BlockquoteParser::process(Line &currentLine,
                                     TextStream &stream,
                                     QSharedPointer<Document> doc,
                                     QSharedPointer<Block> parent,
                                     Context &ctx,
                                     const QString &path,
                                     const QString &fileName,
                                     QStringList &linksToParse)
{
    if (isEmptyLine(currentLine) && !m_quotes.contains(&ctx)) {
        currentLine.skip();

        return BlockState::Continue;
    }

    currentLine.saveState();

    skipSpaces(currentLine);

    QSharedPointer<Blockquote> quote;

    if (!m_quotes.contains(&ctx)) {
        quote = QSharedPointer<Blockquote>::create();
        quote->setStartColumn(currentLine.position());
        quote->setStartLine(currentLine.lineNumber());
        parent->appendItem(quote);
        ctx.setItem(quote.get());
        m_quotes.insert(&ctx, quote);
    } else {
        quote = m_quotes[&ctx];
    }

    if (isBlockquoteOrLazyLine(currentLine, ctx)) {
        if (currentLine.currentChar() == s_greaterSignChar) {
            quote->delims().append(
                {currentLine.column(), currentLine.lineNumber(), currentLine.column(), currentLine.lineNumber()});

            processGreaterSign(currentLine, ctx);
        }

        BlockParser::process(currentLine, stream, doc, quote, ctx, path, fileName, linksToParse);

        quote->setEndColumn(currentLine.length() - 1);
        quote->setEndLine(currentLine.lineNumber());

        return BlockState::Continue;
    } else {
        currentLine.restoreState();

        return BlockState::Stop;
    }
}

QSharedPointer<Block> BlockquoteParser::currentBlock(const Context &ctx) const
{
    return m_quotes.value(&ctx, nullptr);
}

void BlockquoteParser::reset(Context &ctx)
{
    if (m_quotes.contains(&ctx)) {
        m_quotes.remove(&ctx);
    }

    BlockParser::reset(ctx);

    ctx.childIndents().clear();
}

void BlockquoteParser::resetOnAllContexts()
{
    m_quotes.clear();
}

bool BlockquoteParser::mayBreakParagraph(Line &,
                                         TextStream &,
                                         QSharedPointer<Document>,
                                         Context &) const
{
    return true;
}

} /* namespace MD */
