/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "list_parser.h"
#include "constants.h"
#include "context.h"
#include "paragraph_parser.h"
#include "setext_heading_parser.h"
#include "utils.h"

namespace MD
{

ListParser::ListParser(Parser *parser)
    : BlockParser(parser)
{
}

ListParser::~ListParser() = default;

bool isOrderedList(Line &line,
                   int *num = nullptr,
                   int *len = nullptr,
                   QChar *delim = nullptr)
{
    const auto dp = line.position();

    skipIf(line, [](const QChar &ch) {
        return ch.isDigit();
    });

    if (dp != line.position() && line.position() < line.length()) {
        const auto digits = line.view().sliced(dp, line.position() - dp);

        if (digits.length() > 9) {
            return false;
        }

        const auto i = digits.toInt();

        if (num) {
            *num = i;
        }

        if (len) {
            *len = line.position() - dp;
        }

        if (line.currentChar() == s_dotChar || line.currentChar() == s_rightParenthesisChar) {
            if (delim) {
                *delim = line.currentChar();
            }

            return true;
        }
    }

    return false;
}

inline bool isList(Line &line,
                   Context *lastChild,
                   int *num = nullptr,
                   int *len = nullptr,
                   QChar *delim = nullptr)
{
    const auto rollback = line.makeRollbackObject();

    if (line.currentChar() == s_minusChar
        || line.currentChar() == s_plusSignChar
        || line.currentChar() == s_asteriskChar) {
        if (delim) {
            *delim = line.currentChar();
        }

        if (line.currentChar() == s_minusChar && lastChild) {
            if (dynamic_cast<SetextHeadingParser *>(lastChild->block())
                || SetextHeadingParser::isSetext(line, *lastChild)) {
                return false;
            }
        }

        line.nextChar();

        return (line.currentChar() == s_spaceChar || line.position() == line.length());
    } else {
        const auto ordered = isOrderedList(line, num, len, delim);

        line.nextChar();

        return (ordered && (line.currentChar() == s_spaceChar || line.position() == line.length()));
    }
}

inline void skipList(Line &line)
{
    if (line.currentChar() == s_minusChar
        || line.currentChar() == s_plusSignChar
        || line.currentChar() == s_asteriskChar) {
        line.nextChar();
    } else {
        isOrderedList(line);
        line.nextChar();
    }
}

inline bool isFormedList(Line &line,
                         bool &lastItemIsEmpty,
                         bool &space)
{
    lastItemIsEmpty = isEmptyLine(line);
    space = line.currentChar() == s_spaceChar;

    return (space || lastItemIsEmpty);
}

BlockState ListParser::check(Line &currentLine,
                             TextStream &stream,
                             QSharedPointer<Document> doc,
                             Context &ctx,
                             const QString &path,
                             const QString &fileName,
                             bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (ctx.isInIndent(currentLine.column())) {
        QChar delim;
        if (isList(currentLine,
                   (ctx.children().isEmpty() ? nullptr : &ctx.children().back()),
                   nullptr,
                   nullptr,
                   &delim)) {
            bool dummy = false;
            bool space;

            skipList(currentLine);

            if (isFormedList(currentLine, (checkWithoutProcessing ? dummy : m_lastItemIsEmpty), space)) {
                // This is processing of list item inside list item in one line, like "* * list"
                // In this case we should not append new child context so returning ContinueWithoutAppendingChildCtx.
                if (ctx.parent() && ctx.parent()->block() && ctx.parent()->block() == this) {
                    currentLine.restoreState();

                    const auto state = continueCheck(currentLine, stream, doc, *ctx.parent(), path, fileName);

                    return (state != BlockState::Stop ? BlockState::ContinueWithoutAppendingChildCtx : state);
                }

                if (!checkWithoutProcessing) {
                    ctx.setFirstLineNumber(currentLine.lineNumber());
                    ctx.setTopListDelim(delim);
                }

                const auto c =
                    (currentLine.position() < currentLine.length() ? currentLine.column() : currentLine.column() - 1);
                const auto stSp = currentLine.currentState();

                skipSpaces(currentLine);

                const auto column =
                    (currentLine.column() - c > 4 ? c : (m_lastItemIsEmpty ? c : currentLine.column() - 1))
                    + (space ? 0 : 1);

                if (column == c) {
                    currentLine.restoreState(&stSp);
                    currentLine.nextChar();
                }

                if (!checkWithoutProcessing) {
                    ctx.appendChildIndent(indentFromColumn(column), delim);
                }

                if (checkWithoutProcessing) {
                    currentLine.restoreState();
                }

                return BlockState::Continue;
            }
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

inline void applyLastPosition(const Context &ctx,
                              qsizetype lastPos,
                              qsizetype lastLine)
{
    for (const auto &l : ctx.lists()) {
        l->setEndColumn(lastPos);
        l->setEndLine(lastLine);
        l->items().back()->setEndColumn(lastPos);
        l->items().back()->setEndLine(lastLine);
    }
}

void ListParser::finishFirstChild(Line &currentLine,
                                  TextStream &stream,
                                  QSharedPointer<Document> doc,
                                  Context &ctx,
                                  const QString &path,
                                  const QString &fileName,
                                  bool makeListItem,
                                  QStringList &linksToParse)
{
    if (m_lastBlockState == BlockState::Continue && makeListItem) {
        if (ctx.children().front().block()) {
            if (ctx.children().front().firstLineNumber() < currentLine.lineNumber()) {
                ctx.children().front().block()->finish(currentLine,
                                                       stream,
                                                       doc,
                                                       m_lastListItem,
                                                       ctx.children().front(),
                                                       path,
                                                       fileName,
                                                       linksToParse);
                ctx.children().front().block()->reset(ctx.children().front());
                ctx.children().dequeue();

                m_lastBlockState = BlockState::None;
            }
        }
    }
}

ListParser::ListProcessState ListParser::processList(Line &currentLine,
                                                     TextStream &stream,
                                                     QSharedPointer<Document> doc,
                                                     Context &ctx,
                                                     const QString &path,
                                                     const QString &fileName,
                                                     bool makeListItem,
                                                     QStringList &linksToParse)
{
    QSharedPointer<ListItem> item;

    currentLine.saveState();

    const auto lazy = (!makeListItem ? isLazyContinuationLine(currentLine, stream, doc, ctx, true)
                                     : ctx.isLazyLine(currentLine.lineNumber()));

    skipSpaces(currentLine);

    const auto spacesCount = currentLine.column();
    const auto emptyLine = isEmptyLine(currentLine);
    const auto startPos = currentLine.position();
    int num = -1;
    int len = -1;
    QChar delim;
    const auto list =
        isList(currentLine,
               (ctx.children().isEmpty() ? nullptr : (makeListItem ? &ctx.children().front() : &ctx.children().back())),
               &num,
               &len,
               &delim);
    const auto hr = isHorizontalLine(currentLine);

    if (m_lastItemIsEmpty && !emptyLine && !list) {
        if (currentLine.column() < ctx.firstChildIndent() + 1) {
            return {BlockState::Stop, item, false};
        } else {
            m_lastItemIsEmpty = false;
        }
    } else if (m_lastItemIsEmpty && emptyLine) {
        ctx.clearChildIndents(ctx.lastChildIndent(), false);

        m_lastItemIsEmpty = false;
    }

    auto formedListItem = false;

    auto *childCtx =
        (!ctx.children().isEmpty() ? (makeListItem ? &ctx.children().front() : &ctx.children().back()) : nullptr);

    if ((list && currentLine.column() >= ctx.indentColumn())
        || lazy
        || (ctx.hasChildIndents() ? currentLine.column() >= ctx.firstChildIndent() : false)) {
        if (childCtx && childCtx->isNotFinished() && (spacesCount > ctx.lastChildIndent() || emptyLine)) {
            return {BlockState::Continue, item, false};
        } else if (childCtx
                   && childCtx->isNotFinished()
                   && childCtx->block()
                   && childCtx->block()->isNotFinishedDiscardable()) {
            childCtx->setDiscardForced(true);
            childCtx->setNotFinished(false);

            return {BlockState::Continue, item, false};
        } else if (lazy) {
            ctx.appendLazyInfo(currentLine.lineNumber());

            return {BlockState::Continue, item, false};
        } else if (list && !hr) {
            if (spacesCount < ctx.lastChildIndent() + 4
                && (spacesCount > ctx.firstChildIndent() || spacesCount < indentFromColumn(ctx.indentColumn()) + 4)) {
                skipList(currentLine);

                bool space;

                if (isFormedList(currentLine, m_lastItemIsEmpty, space)) {
                    ctx.setFirstLineNumber(currentLine.lineNumber());
                    formedListItem = true;

                    finishFirstChild(currentLine, stream, doc, ctx, path, fileName, makeListItem, linksToParse);

                    const auto c = (currentLine.position() < currentLine.length() ? currentLine.column()
                                                                                  : currentLine.column() - 1);
                    const auto stSp = currentLine.currentState();

                    skipSpaces(currentLine);

                    const auto column =
                        (currentLine.column() - c > 4 ? c : (m_lastItemIsEmpty ? c : currentLine.column() - 1))
                        + (space ? 0 : 1);

                    if (column == c) {
                        currentLine.restoreState(&stSp);
                        currentLine.nextChar();
                    }

                    if (makeListItem) {
                        item = QSharedPointer<ListItem>::create();
                        item->setStartColumn(startPos);
                        item->setStartLine(currentLine.lineNumber());
                        item->setEndColumn(currentLine.position() - 1);
                        item->setEndLine(currentLine.lineNumber());

                        if (num != -1) {
                            item->setListType(ListItem::Ordered);
                            item->setStartNumber(num);
                            item->setDelim(
                                {startPos, currentLine.lineNumber(), startPos + len, currentLine.lineNumber()});
                            item->setOrderedListPreState(num == 1 ? ListItem::Start : ListItem::Continue);
                        } else {
                            item->setListType(ListItem::Unordered);
                            item->setDelim({startPos, currentLine.lineNumber(), startPos, currentLine.lineNumber()});
                        }
                    } else if (ctx.children().back().block() && !ctx.children().back().isDiscardForced()) {
                        Context child(&ctx);
                        child.applyParentContext(ctx);
                        ctx.children().enqueue(child);
                    }

                    const auto was = ctx.listDelim(spacesCount);

                    ctx.clearChildIndents(spacesCount, false);

                    if (was != delim) {
                        if (ctx.childIndents().isEmpty()) {
                            currentLine.restoreState();

                            return {BlockState::Stop, item, false};
                        } else {
                            ctx.appendChildIndent(indentFromColumn(column), delim);

                            return {BlockState::Continue, item, true};
                        }
                    }

                    ctx.appendChildIndent(indentFromColumn(column), delim);
                }
            }
        }

        if (!formedListItem && !emptyLine) {
            const auto indentsCount = ctx.childIndents().size();

            ctx.clearChildIndents(spacesCount, true);

            if (indentsCount != ctx.childIndents().size()) {
                finishFirstChild(currentLine, stream, doc, ctx, path, fileName, makeListItem, linksToParse);
            }

            if (!makeListItem) {
                ctx.children().back().applyParentContext(ctx);
            } else {
                ctx.updateParentContextForAllChildren();
            }
        }

        if (formedListItem || (ctx.hasChildIndents() && spacesCount > ctx.firstChildIndent())) {
            return {BlockState::Continue, item, false};
        }
    } else if (childCtx
               && childCtx->isNotFinished()
               && childCtx->block()
               && childCtx->block()->isNotFinishedDiscardable()) {
        childCtx->setDiscardForced(true);
        childCtx->setNotFinished(false);

        return {BlockState::Continue, item, false};
    }

    if (emptyLine) {
        return {BlockState::Continue, item, false};
    }

    currentLine.restoreState();

    return {BlockState::Stop, item, false};
}

BlockState ListParser::continueCheck(Line &currentLine,
                                     TextStream &stream,
                                     QSharedPointer<Document> doc,
                                     Context &ctx,
                                     const QString &path,
                                     const QString &fileName)
{
    QStringList dummy;
    return processList(currentLine, stream, doc, ctx, path, fileName, false, dummy).m_state;
}

void ListParser::setLastListItem(Context &ctx)
{
    if (!ctx.lists().isEmpty() && !ctx.lists().back()->items().isEmpty()) {
        m_lastListItem = ctx.lists().back()->items().back().staticCast<ListItem>();
        ctx.setItem(m_lastListItem.get());
    }
}

BlockState ListParser::process(Line &currentLine,
                               TextStream &stream,
                               QSharedPointer<Document> doc,
                               QSharedPointer<Block> parent,
                               Context &ctx,
                               const QString &path,
                               const QString &fileName,
                               QStringList &linksToParse)
{
    skipSpaces(currentLine);

    if (!m_list) {
        if (isEmptyLine(currentLine)) {
            currentLine.skip();

            return BlockState::Continue;
        }

        m_list = QSharedPointer<List>::create();
        m_list->setStartColumn(currentLine.position());
        m_list->setStartLine(currentLine.lineNumber());
        parent->appendItem(m_list);
        ctx.lists().append(m_list);
    }

    const auto emptyLine = isEmptyLine(currentLine);

    ListProcessState state;
    Line::State lineState = currentLine.currentState();
    QChar bullet = currentLine.currentChar();
    auto looped = false;
    auto column = currentLine.column();
    auto pos = currentLine.position();

    while (isList(currentLine, (ctx.children().isEmpty() ? nullptr : &ctx.children().front()))
           && !ctx.isLazyLine(currentLine.lineNumber())
           && (!ctx.children().isEmpty()
                   ? (ctx.children().front().isNotFinished()
                          ? currentLine.column() <= ctx.children().front().indentColumn() && !emptyLine
                          : true)
                   : true)
           && column < indentFromColumn(ctx.lastChildIndent()) + 4
           && pos < currentLine.length()) {
        looped = true;
        currentLine.restoreState(&lineState);

        setLastListItem(ctx);

        state = processList(currentLine, stream, doc, ctx, path, fileName, true, linksToParse);

        if (state.m_state != BlockState::Stop) {
            if (state.m_item) {
                if (ctx.childIndents().size() > ctx.lists().size()) {
                    auto list = QSharedPointer<List>::create();
                    list->setStartColumn(state.m_item->startColumn());
                    list->setStartLine(state.m_item->startLine());
                    if (ctx.parent() && ctx.parent()->block() && ctx.parent()->item()) {
                        static_cast<Block *>(ctx.parent()->item())->appendItem(list);
                    } else {
                        m_lastListItem->appendItem(list);
                    }
                    ctx.lists().append(list);
                } else if (state.m_makeNewList) {
                    auto list = QSharedPointer<List>::create();
                    list->setStartColumn(state.m_item->startColumn());
                    list->setStartLine(state.m_item->startLine());
                    ctx.lists().pop_back();
                    ctx.lists().back()->items().back().staticCast<ListItem>()->appendItem(list);
                    ctx.lists().append(list);
                }

                ctx.lists().back()->appendItem(state.m_item);
            }

            setLastListItem(ctx);

            applyLastPosition(ctx, currentLine.length() - 1, currentLine.lineNumber());
        } else {
            break;
        }

        ParagraphParser *p = (!ctx.children().isEmpty() && ctx.children().front().block()
                                  ? dynamic_cast<ParagraphParser *>(ctx.children().front().block())
                                  : nullptr);

        if (p && !(ctx.children().size() > 1 && dynamic_cast<SetextHeadingParser *>(ctx.children()[1].block()))) {
            if (ctx.children().front().firstLineNumber() == currentLine.lineNumber()) {
                const auto st = currentLine.currentState();

                skipSpaces(currentLine);

                if (currentLine.currentChar() == s_leftSquareBracketChar) {
                    const auto startTaskDelimPos = currentLine.position();

                    currentLine.nextChar();

                    if (currentLine.currentChar() == s_spaceChar || currentLine.currentChar().toLower() == s_xChar) {
                        const auto checked = currentLine.currentChar().toLower() == s_xChar;

                        currentLine.nextChar();

                        if (currentLine.currentChar() == s_rightSquareBracketChar) {
                            m_lastListItem->setTaskList();
                            m_lastListItem->setTaskDelim({startTaskDelimPos,
                                                          currentLine.lineNumber(),
                                                          currentLine.position(),
                                                          currentLine.lineNumber()});
                            m_lastListItem->setChecked(checked);
                            m_lastListItem->setEndColumn(currentLine.position());

                            currentLine.nextChar();

                            ctx.children().front().updateStartPos(currentLine.lineNumber(), currentLine.position());
                        } else {
                            currentLine.restoreState(&st);
                        }
                    } else {
                        currentLine.restoreState(&st);
                    }
                } else {
                    currentLine.restoreState(&st);
                }
            }
        }

        lineState = currentLine.currentState();
        skipSpaces(currentLine);
        column = currentLine.column();
        pos = currentLine.position();

        const auto hr = isHorizontalLine(currentLine);

        if (hr && bullet != currentLine.currentChar()) {
            state.m_state = BlockState::Continue;

            break;
        }
    }

    currentLine.restoreState(&lineState);

    if (!looped) {
        state = processList(currentLine, stream, doc, ctx, path, fileName, true, linksToParse);

        if (state.m_state != MD::BlockState::Stop) {
            setLastListItem(ctx);
        }
    }

    if (state.m_state != BlockState::Stop) {
        m_lastBlockState =
            BlockParser::process(currentLine, stream, doc, m_lastListItem, ctx, path, fileName, linksToParse);

        applyLastPosition(ctx, currentLine.length() - (currentLine.length() ? 1 : 0), currentLine.lineNumber());
    }

    return state.m_state;
}

void ListParser::reset(Context &ctx)
{
    m_lastItemIsEmpty = false;
    auto *parent = ctx.parent();
    auto clearRootList = true;

    while (parent) {
        if (parent->block() && !parent->lists().isEmpty()) {
            clearRootList = false;
            break;
        }

        parent = parent->parent();
    }

    if (clearRootList) {
        m_list.reset();
        m_lastListItem.reset();
    }

    BlockParser::reset(ctx);

    ctx.childIndents().clear();
    ctx.lists().clear();
}

void ListParser::resetOnAllContexts()
{
    m_list.reset();
    m_lastListItem.reset();
    m_lastItemIsEmpty = false;
    m_lastBlockState = BlockState::None;
}

bool ListParser::mayBreakParagraph(Line &line,
                                   TextStream &,
                                   QSharedPointer<Document>,
                                   Context &ctx) const
{
    const auto st = line.currentState();

    skipSpaces(line);

    int num = -1;
    bool emptyListItem = false;
    bool space;

    auto ret = line.column() < ctx.indentColumn() + 4;
    ret = ret && isList(line, nullptr, &num);
    skipList(line);
    ret = ret && isFormedList(line, emptyListItem, space) && !emptyListItem;

    if (num >= 0) {
        ret = ret && num == 1;
    }

    line.restoreState(&st);

    return ret;
}

QSharedPointer<Block> ListParser::currentBlock(const Context &) const
{
    return m_lastListItem;
}

} /* namespace MD */
