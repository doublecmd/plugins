/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "parser.h"
#include "asterisk_emphasis_parser.h"
#include "atx_heading_parser.h"
#include "autolink_parser.h"
#include "blockquote_parser.h"
#include "constants.h"
#include "context.h"
#include "emphasis_parser.h"
#include "fenced_code_parser.h"
#include "footnote_parser.h"
#include "gfm_autolink_parser.h"
#include "hard_line_break_parser.h"
#include "html_parser.h"
#include "indented_code_parser.h"
#include "inline_code_parser.h"
#include "inline_html_parser.h"
#include "inline_math_parser.h"
#include "link_image_parser.h"
#include "list_parser.h"
#include "paragraph_parser.h"
#include "setext_heading_parser.h"
#include "strikethrough_emphasis_parser.h"
#include "table_parser.h"
#include "text_stream.h"
#include "thematic_break_parser.h"
#include "underline_emphasis_parser.h"
#include "utils.h"
#include "yaml_parser.h"

// Qt include.
#include <QFileInfo>

// C++ include.
#include <algorithm>
#include <functional>

namespace MD
{

//
// Parser
//

Parser::Parser()
{
    setBlockParsers(makeDefaultBlockParsersPipeline(this));
    setInlineParsers(makeDefaultInlineParsersPipeline());
}

Parser::~Parser() = default;

QSharedPointer<Document> Parser::parse(const QString &fileName,
                                       bool recursive,
                                       const QStringList &ext)
{
    QSharedPointer<Document> doc(new Document);

    parseFile(fileName, recursive, doc, ext);

    reset();

    return doc;
}

QSharedPointer<Document> Parser::parse(QTextStream &stream,
                                       const QString &path,
                                       const QString &fileName)
{
    QSharedPointer<Document> doc(new Document);

    parseStream(stream, path, fileName, false, doc, QStringList());

    reset();

    return doc;
}

QSharedPointer<Document> Parser::parse(const QString &fileName,
                                       const QString &workingDirectory,
                                       bool recursive,
                                       const QStringList &ext)
{
    QSharedPointer<Document> doc(new Document);

    QString wd;
    const auto i = skipIfBackward(workingDirectory.length() - 1, workingDirectory, [](const QChar &ch) {
        return (ch == s_reverseSolidusChar || ch == s_solidusChar);
    });

    if ((i == 0 && workingDirectory[i] == s_solidusChar) || i < 0) {
        wd = QString(s_solidusChar);
    } else if (i > 0) {
        wd = workingDirectory.sliced(0, i + 1);
    }

    parseFile(fileName, recursive, doc, ext, nullptr, wd);

    reset();

    return doc;
}

Parser::BlockParsers Parser::makeDefaultBlockParsersPipeline(Parser *parser)
{
    BlockParsers parsers;

    appendBlockParser<YAMLParser>(parsers, parser);
    appendBlockParser<BlockquoteParser>(parsers, parser);
    appendBlockParser<SetextHeadingParser>(parsers, parser);
    appendBlockParser<ThematicBreakParser>(parsers, parser);
    appendBlockParser<ListParser>(parsers, parser);
    appendBlockParser<ATXHeadingParser>(parsers, parser);
    appendBlockParser<FencedCodeParser>(parsers, parser);
    appendBlockParser<HTMLParser>(parsers, parser);
    appendBlockParser<IndentedCodeParser>(parsers, parser);
    appendBlockParser<FootnoteParser>(parsers, parser);
    appendBlockParser<TableParser>(parsers, parser);
    appendBlockParser<ParagraphParser>(parsers, parser);

    return parsers;
}

Parser::InlineParsers Parser::makeDefaultInlineParsersPipeline()
{
    InlineParsers parsers;

    appendInlineParser<InlineCodeParser>(parsers);

    auto linkParser = appendInlineParser<LinkImageParser>(parsers);

    appendInlineParser<AutolinkParser>(parsers);
    appendInlineParser<InlineHtmlParser>(parsers);
    appendInlineParser<InlineMathParser>(parsers);
    appendInlineParser<AsteriskEmphasisParser>(parsers);
    appendInlineParser<UnderlineEmphasisParser>(parsers);
    appendInlineParser<StrikethroughEmphasisParser>(parsers);

    parsers.append(QSharedPointer<GfmAutolinkParser>::create(linkParser));

    appendInlineParser<HardLineBreakParser>(parsers);

    return parsers;
}

Parser::BlockParsers Parser::makeCommonMarkBlockParsersPipeline(Parser *parser)
{
    BlockParsers parsers;

    appendBlockParser<BlockquoteParser>(parsers, parser);
    appendBlockParser<SetextHeadingParser>(parsers, parser);
    appendBlockParser<ThematicBreakParser>(parsers, parser);
    appendBlockParser<ListParser>(parsers, parser);
    appendBlockParser<ATXHeadingParser>(parsers, parser);
    appendBlockParser<FencedCodeParser>(parsers, parser);
    appendBlockParser<HTMLParser>(parsers, parser);
    appendBlockParser<IndentedCodeParser>(parsers, parser);
    appendBlockParser<ParagraphParser>(parsers, parser);

    return parsers;
}

Parser::InlineParsers Parser::makeCommonMarkInlineParsersPipeline()
{
    InlineParsers parsers;

    appendInlineParser<InlineCodeParser>(parsers);
    appendInlineParser<LinkImageParser>(parsers);
    appendInlineParser<AutolinkParser>(parsers);
    appendInlineParser<InlineHtmlParser>(parsers);
    appendInlineParser<AsteriskEmphasisParser>(parsers);
    appendInlineParser<UnderlineEmphasisParser>(parsers);
    appendInlineParser<HardLineBreakParser>(parsers);

    return parsers;
}

BlockParser *Parser::checkBlock(Line &line,
                                TextStream &stream,
                                QSharedPointer<Document> doc,
                                Context &ctx) const
{
    if (line.position() < line.length()) {
        for (auto it = m_blockParsers.begin(), last = m_blockParsers.end(); it != last; ++it) {
            const auto state = (*it)->check(line, stream, doc, ctx, QString(), QString(), true);

            if (state != BlockState::None) {
                return it->get();
            }
        }
    }

    return nullptr;
}

BlockParser *Parser::checkBlockExcluding(Line &line,
                                         TextStream &stream,
                                         QSharedPointer<Document> doc,
                                         Context &ctx,
                                         const BlockParser *exclude) const
{
    if (line.position() < line.length()) {
        for (auto it = m_blockParsers.begin(), last = m_blockParsers.end(); it != last; ++it) {
            if (it->get() != exclude) {
                const auto state = (*it)->check(line, stream, doc, ctx, QString(), QString(), true);

                if (state != BlockState::None) {
                    return it->get();
                }
            }
        }
    }

    return nullptr;
}

void Parser::parseFile(const QString &fileName,
                       bool recursive,
                       QSharedPointer<Document> doc,
                       const QStringList &ext,
                       QStringList *parentLinks,
                       QString workingDirectory)
{
    QFileInfo fi(fileName);

    if (fi.exists() && ext.contains(fi.suffix().toLower())) {
        if (!doc->isEmpty() && doc->items().back()->type() != ItemType::PageBreak) {
            doc->appendItem(QSharedPointer<PageBreak>(new PageBreak));
        }

        QFile f(fileName);

        if (f.open(QIODevice::ReadOnly)) {
            QTextStream s(f.readAll());
            f.close();

            auto wd = fi.absolutePath();
            auto fn = fi.fileName();

            workingDirectory.replace(s_reverseSolidusChar, s_solidusChar);

            if (!workingDirectory.isEmpty() && wd.contains(workingDirectory)) {
                QFileInfo folder(workingDirectory);

                if (folder.exists() && folder.isDir()) {
                    wd = folder.absoluteFilePath();

                    auto tmp = fi.absoluteFilePath();
                    fn = tmp.remove(wd);
                    fn.removeAt(0);
                }
            }

            parseStream(s, wd, fn, recursive, doc, ext, parentLinks, workingDirectory);
        }
    }
}

void resolveLinks(QStringList &linksToParse,
                  QSharedPointer<Document> doc)
{
    for (auto it = linksToParse.begin(), last = linksToParse.end(); it != last; ++it) {
        auto nextFileName = *it;

        if (nextFileName.startsWith(s_numberSignChar)) {
            const auto lit = doc->labeledLinks().find(nextFileName);

            if (lit != doc->labeledLinks().cend()) {
                nextFileName = (*lit)->url();
            } else {
                continue;
            }
        }

        if (QFileInfo::exists(nextFileName)) {
            *it = QFileInfo(nextFileName).absoluteFilePath();
        }
    }
}

void Parser::parse(QTextStream &s,
                   QSharedPointer<Document> doc,
                   const QString &path,
                   const QString &fileName,
                   QStringList &linksToParse)
{
    TextStream stream(s);
    Context ctx;
    Context child;
    child.applyParentContext(ctx);
    ctx.children().enqueue(child);

    stream.saveState();

    while (!stream.atEnd()) {
        auto line = stream.readLine();
        auto empty = isEmptyLine(line);

        ParseState state;

        const auto moveToDiscardedIf = [&state, &line, &stream, &empty]() -> bool {
            if (state.m_state == BlockState::Discard) {
                if (state.m_context && state.m_context->firstLineNumber() != -1) {
                    line = stream.moveTo(state.m_context->firstLineNumber());
                    empty = isEmptyLine(line);

                    return true;
                }
            }

            return false;
        };

        do {
            moveToDiscardedIf();

            parse(line, stream, doc, ctx, path, fileName, linksToParse, state);
        } while (state.m_state == BlockState::Discard);

        if (stream.atEnd() && !empty) {
            line = Line(QStringView(), line.lineNumber() + 1);
            auto discarded = false;

            do {
                discarded = moveToDiscardedIf();

                parse(line, stream, doc, ctx, path, fileName, linksToParse, state);
            } while (state.m_state == BlockState::Discard);

            if (stream.atEnd() && !empty && discarded) {
                line = Line(QStringView(), line.lineNumber() + 1);

                parse(line, stream, doc, ctx, path, fileName, linksToParse, state);
            }
        }

        if (stream.atEnd() && !ctx.children().isEmpty() && ctx.children().back().block()) {
            line = Line(QStringView(), line.lineNumber() + 1);

            ctx.children()
                .back()
                .block()
                ->finish(line, stream, doc, nullptr, ctx.children().back(), path, fileName, linksToParse);
            ctx.children().back().block()->reset(ctx.children().back());
        }
    }

    stream.restoreState();

    resetParsers();

    bool readNewLine = true;
    Line line(QStringView(), -1);

    while (!stream.atEnd()) {
        if (readNewLine) {
            line = stream.readLine();
        }

        readNewLine = true;

        auto *childCtx = &ctx.children().head();

        if (childCtx->block()) {
            const auto state =
                childCtx->block()->process(line, stream, doc, doc, *childCtx, path, fileName, linksToParse);

            if (state == BlockState::Stop) {
                if (isEmptyLine(line)) {
                    line.skip();
                }

                if (line.position() < line.length()) {
                    readNewLine = false;
                }

                childCtx->block()->finish(line, stream, doc, doc, *childCtx, path, fileName, linksToParse);
                childCtx->block()->reset(*childCtx);

                ctx.children().dequeue();

                childCtx = &ctx.children().head();
            }
        }

        if (stream.atEnd() && childCtx->block()) {
            if (line.position() < line.length()) {
                childCtx->block()->process(line, stream, doc, doc, *childCtx, path, fileName, linksToParse);
            }

            line = Line(QStringView(), line.lineNumber() + 1);

            childCtx->block()->finish(line, stream, doc, doc, *childCtx, path, fileName, linksToParse);
            childCtx->block()->reset(*childCtx);
        }
    }
}

void Parser::loopBlockParsers(Line &currentLine,
                              TextStream &stream,
                              QSharedPointer<Document> doc,
                              Context &ctx,
                              const QString &path,
                              const QString &fileName,
                              QStringList &linksToParse,
                              ParseState &state)
{
    if (currentLine.position() < currentLine.length()) {
        for (auto it = m_blockParsers.begin(), last = m_blockParsers.end(); it != last; ++it) {
            if (&ctx.children().back() == state.m_context && state.m_skip.contains(it->get())) {
                continue;
            }

            const auto st = (*it)->check(currentLine, stream, doc, ctx.children().back(), path, fileName);

            if (st != BlockState::None) {
                if (ctx.children().size() > 1
                    && (*it) == ctx.children()[ctx.children().size() - 2].block()
                    && ctx.children()[ctx.children().size() - 2].lastLineNumber() != -1
                    && ctx.children()[ctx.children().size() - 2].lastLineNumber() + 1 == currentLine.lineNumber()
                    && ctx.firstLineNumber() != currentLine.lineNumber()) {
                    ctx.children()[ctx.children().size() - 2].mergeLineInfo(ctx.children().back().lineInfo());
                    ctx.children().pop_back();
                    ctx.children().back().setLastLineNumber(currentLine.lineNumber());
                }

                if (st != BlockState::ContinueWithoutAppendingChildCtx) {
                    ctx.children().back().setBlock(it->get());
                }

                if (st != BlockState::Stop) {
                    if (st != BlockState::ContinueWithoutAppendingChildCtx) {
                        Context child(&ctx.children().back());
                        child.applyParentContext(ctx.children().back());
                        ctx.children().back().children().enqueue(child);
                    }

                    parse(currentLine,
                          stream,
                          doc,
                          (st != BlockState::ContinueWithoutAppendingChildCtx ? ctx.children().back() : ctx),
                          path,
                          fileName,
                          linksToParse,
                          state);
                } else {
                    (*it)->finish(currentLine,
                                  stream,
                                  doc,
                                  nullptr,
                                  ctx.children().back(),
                                  path,
                                  fileName,
                                  linksToParse);
                    (*it)->reset(ctx.children().back());

                    Context child(&ctx);
                    child.applyParentContext(ctx);
                    ctx.children().enqueue(child);
                }

                break;
            }
        }
    }
}

void Parser::parse(Line &currentLine,
                   TextStream &stream,
                   QSharedPointer<Document> doc,
                   Context &ctx,
                   const QString &path,
                   const QString &fileName,
                   QStringList &linksToParse,
                   ParseState &state)
{
    if (ctx.children().back().block()) {
        if (state.m_state == BlockState::Discard && &ctx.children().back() == state.m_context) {
            ctx.children().back().setBlock(nullptr);
            ctx.children().back().setDiscardForced(false);
            ctx.children().back().children().clear();
            state.m_state = BlockState::None;

            loopBlockParsers(currentLine, stream, doc, ctx, path, fileName, linksToParse, state);
        } else {
            const auto st = ctx.children().back().block()->continueCheck(currentLine,
                                                                         stream,
                                                                         doc,
                                                                         ctx.children().back(),
                                                                         path,
                                                                         fileName);

            if (st == BlockState::Stop) {
                ctx.children()
                    .back()
                    .block()
                    ->finish(currentLine, stream, doc, nullptr, ctx.children().back(), path, fileName, linksToParse);
                ctx.children().back().block()->reset(ctx.children().back());

                Context child;
                child.applyParentContext(ctx);
                ctx.children().enqueue(child);

                loopBlockParsers(currentLine, stream, doc, ctx, path, fileName, linksToParse, state);
            } else if (st == BlockState::Discard) {
                if (state.m_context != &ctx.children().back()) {
                    state.m_skip.clear();
                }

                state.m_context = &ctx.children().back();
                state.m_state = BlockState::Discard;
                state.m_skip.insert(ctx.children().back().block());
            } else {
                parse(currentLine, stream, doc, ctx.children().back(), path, fileName, linksToParse, state);
            }
        }
    } else {
        loopBlockParsers(currentLine, stream, doc, ctx, path, fileName, linksToParse, state);
    }
}

void Parser::parseStream(QTextStream &s,
                         const QString &path,
                         const QString &fileName,
                         bool recursive,
                         QSharedPointer<Document> doc,
                         const QStringList &ext,
                         QStringList *parentLinks,
                         const QString &workingDirectory)
{
    QStringList linksToParse;

    const auto anchor = path.isEmpty() ? QString(fileName) : QString(path + s_solidusChar + fileName);

    doc->appendItem(QSharedPointer<Anchor>(new Anchor(anchor)));

    parse(s, doc, path, fileName, linksToParse);

    m_parsedFiles.push_back(anchor);

    resolveLinks(linksToParse, doc);

    // Parse all links if parsing is recursive.
    if (recursive && !linksToParse.empty()) {
        const auto tmpLinks = linksToParse;

        while (!linksToParse.empty()) {
            auto nextFileName = linksToParse.front();
            linksToParse.erase(linksToParse.cbegin());

            if (parentLinks) {
                const auto pit = std::find(parentLinks->cbegin(), parentLinks->cend(), nextFileName);

                if (pit != parentLinks->cend()) {
                    continue;
                }
            }

            if (nextFileName.startsWith(s_numberSignChar)) {
                continue;
            }

            const auto pit = std::find(m_parsedFiles.cbegin(), m_parsedFiles.cend(), nextFileName);

            if (pit == m_parsedFiles.cend()) {
                parseFile(nextFileName, recursive, doc, ext, &linksToParse, workingDirectory);
            }
        }

        if (parentLinks) {
            std::copy(tmpLinks.cbegin(), tmpLinks.cend(), std::back_inserter(*parentLinks));
        }
    }
}

void Parser::resetParsers()
{
    std::for_each(m_blockParsers.begin(), m_blockParsers.end(), [](auto &parser) {
        parser->resetOnAllContexts();
    });
}

void Parser::reset()
{
    m_parsedFiles.clear();
    resetParsers();
}

} /* namespace MD */
