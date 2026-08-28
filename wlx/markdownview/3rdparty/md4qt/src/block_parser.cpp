/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "block_parser.h"
#include "paragraph_parser.h"
#include "parser.h"

namespace MD
{

BlockParser::BlockParser(Parser *parser)
    : m_parser(parser)
{
}

BlockParser::~BlockParser() = default;

Parser *BlockParser::parser() const
{
    return m_parser;
}

BlockState BlockParser::processOneTime(Line &currentLine,
                                       TextStream &stream,
                                       QSharedPointer<Document> doc,
                                       QSharedPointer<Block> parent,
                                       Context &ctx,
                                       const QString &path,
                                       const QString &fileName,
                                       QStringList &linksToParse)
{
    BlockState state = BlockState::None;
    if (!ctx.children().isEmpty() && ctx.children().head().block()) {
        state = ctx.children()
                    .head()
                    .block()
                    ->process(currentLine, stream, doc, parent, ctx.children().head(), path, fileName, linksToParse);

        if (state == BlockState::Stop) {
            ctx.children()
                .head()
                .block()
                ->finish(currentLine, stream, doc, parent, ctx.children().head(), path, fileName, linksToParse);
            ctx.children().head().block()->reset(ctx.children().head());
            ctx.children().dequeue();
        }
    }

    return state;
}

BlockState BlockParser::process(Line &currentLine,
                                TextStream &stream,
                                QSharedPointer<Document> doc,
                                QSharedPointer<Block> parent,
                                Context &ctx,
                                const QString &path,
                                const QString &fileName,
                                QStringList &linksToParse)
{
    BlockState state = BlockState::None;

    do {
        state = processOneTime(currentLine, stream, doc, parent, ctx, path, fileName, linksToParse);
    } while (!ctx.children().isEmpty() && ctx.children().head().block() && !currentLine.atEnd());

    return state;
}

void BlockParser::reset(Context &ctx)
{
    for (qsizetype i = 0; i < ctx.children().size(); ++i) {
        if (ctx.children()[i].block()) {
            ctx.children()[i].block()->reset(ctx.children()[i]);
        }
    }
}

bool BlockParser::canBeLazyLine(Line &,
                                TextStream &,
                                QSharedPointer<Document>,
                                Context &) const
{
    return false;
}

void BlockParser::finish(Line &currentLine,
                         TextStream &stream,
                         QSharedPointer<Document> doc,
                         QSharedPointer<Block>,
                         Context &ctx,
                         const QString &path,
                         const QString &fileName,
                         QStringList &linksToParse)
{
    for (qsizetype i = 0; i < ctx.children().size(); ++i) {
        if (ctx.children()[i].block()) {
            ctx.children()[i]
                .block()
                ->finish(currentLine, stream, doc, currentBlock(ctx), ctx.children()[i], path, fileName, linksToParse);
        }
    }
}

QSharedPointer<Block> BlockParser::currentBlock(const Context &) const
{
    return nullptr;
}

bool BlockParser::isNotFinishedDiscardable() const
{
    return false;
}

bool BlockParser::isLazyContinuationLine(Line &line,
                                         TextStream &stream,
                                         QSharedPointer<Document> doc,
                                         Context &ctx,
                                         bool considerIndents) const
{
    const auto &child = ctx.mostNestedChild();
    auto p = dynamic_cast<ParagraphParser *>(child.block());

    if (p) {
        const auto st = line.currentState();

        if (!considerIndents) {
            ctx.setDontConsiderIndents(true);
        }

        auto b = parser()->checkBlock(line, stream, doc, ctx);

        ctx.setDontConsiderIndents(false);

        if (b) {
            return (b->canBeLazyLine(line, stream, doc, ctx) && !b->mayBreakParagraph(line, stream, doc, ctx));
        }

        line.restoreState(&st);
    }

    return false;
}

} /* namespace MD */
