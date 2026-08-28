/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "paragraph_parser.h"
#include "emphasis_parser.h"
#include "inline_context.h"
#include "parser.h"
#include "reverse_solidus.h"
#include "utils.h"

// C++ include.
#include <type_traits>

// Qt include.
#include <QFileInfo>

namespace MD
{

ParagraphParser::ParagraphParser(Parser *parser)
    : BlockParser(parser)
{
}

ParagraphParser::~ParagraphParser() = default;

void ParagraphParser::clearRefLink()
{
    m_reference.reset();
    m_refLinkLabel.clear();
    m_refLinkTitle.clear();
    m_wasSpace = false;
    m_refLinkStage = RefLinkParserStage::S0;
    m_refLinkTextPos = {};
    m_refLinkUrlPos = {};
    m_refLinkTitlePos = {};
    m_refLinkTitleStartChar = {};
    m_refLinkStartParenthesisCount = 0;
    m_refLinkTitleStartPos = -1;
}

inline bool isRefLinkStage0(Line &currentLine,
                            const Context &ctx)
{
    return (currentLine.column() < indentFromColumn(ctx.indentColumn()) + 4
            && currentLine.currentChar() == s_leftSquareBracketChar);
}

ParagraphParser::RefLinkState ParagraphParser::checkForReferenceLink(Line &currentLine,
                                                                     QSharedPointer<Document> doc,
                                                                     const Context &ctx,
                                                                     const QString &path,
                                                                     const QString &fileName)
{
    Line::State st;

    auto returnWrong = [&currentLine, this, &st]() -> RefLinkState {
        currentLine.restoreState(&st);
        this->clearRefLink();
        return RefLinkState::Wrong;
    };

    if (ctx.firstLineNumber() != -1) {
        return RefLinkState::Wrong;
    }

    bool makeReturnWrongOnFail = false;

    {
        if (m_refLinkStage == RefLinkParserStage::S4) {
            const auto st = currentLine.currentState();

            skipSpaces(currentLine);

            if (isRefLinkStage0(currentLine, ctx)) {
                insertRefLink(doc, currentLine.lineNumber());
            } else {
                makeReturnWrongOnFail = true;
            }

            currentLine.restoreState(&st);
        }
    }

    if (!m_reference) {
        m_reference = QSharedPointer<Link>::create();
    }

    st = currentLine.currentState();

    ReverseSolidusHandler rs;

    while (currentLine.position() < currentLine.length()) {
        if (!rs.process(currentLine.currentChar())) {
            auto doBreak = false;

            switch (m_refLinkStage) {
            // [
            case RefLinkParserStage::S0: {
                skipSpaces(currentLine);

                if (isRefLinkStage0(currentLine, ctx)) {
                    m_reference->setStartColumn(currentLine.position());
                    m_reference->setStartLine(currentLine.lineNumber());
                    currentLine.nextChar();
                    rs.next();
                    m_refLinkStage = RefLinkParserStage::S1;
                } else {
                    return returnWrong();
                }
            } break;

            // label
            case RefLinkParserStage::S1: {
                QString label;

                if (currentLine.currentChar() == s_rightSquareBracketChar && !rs.isPrevReverseSolidus()) {
                    m_refLinkStage = RefLinkParserStage::S2;
                    break;
                }

                if (currentLine.position() < currentLine.length()) {
                    m_refLinkTextPos.setStartColumn(currentLine.position());
                    m_refLinkTextPos.setStartLine(currentLine.lineNumber());
                }

                skipSpaces(currentLine);

                while (currentLine.position() < currentLine.length()) {
                    if (currentLine.currentChar() == s_leftSquareBracketChar && !rs.isPrevReverseSolidus()) {
                        return returnWrong();
                    }

                    if (currentLine.currentChar() == s_rightSquareBracketChar && !rs.isPrevReverseSolidus()) {
                        if (!label.simplified().isEmpty()) {
                            m_refLinkStage = RefLinkParserStage::S2;
                            break;
                        } else {
                            return returnWrong();
                        }
                    }

                    label.append(currentLine.currentChar());
                    m_refLinkTextPos.setEndColumn(currentLine.position());
                    m_refLinkTextPos.setEndLine(currentLine.lineNumber());

                    currentLine.nextChar();
                    rs.next();
                    rs.process(currentLine.currentChar());
                }

                if (!label.isEmpty()) {
                    if (!m_refLinkLabel.isEmpty()) {
                        m_refLinkLabel.append(s_spaceChar);
                    }

                    m_refLinkLabel.append(label.simplified());
                }
            } break;

            // ]:
            case RefLinkParserStage::S2: {
                currentLine.nextChar();
                rs.next();

                if (currentLine.currentChar() == s_colonChar
                    && !rs.isPrevReverseSolidus()
                    && m_refLinkLabel.length()
                    && m_refLinkLabel.length() < 1000) {
                    m_refLinkLabel = s_numberSignChar
                        + m_refLinkLabel.toCaseFolded().toUpper()
                        + s_solidusChar
                        + (path.isEmpty() ? QString() : path + s_solidusChar)
                        + fileName;
                    currentLine.nextChar();
                    rs.next();
                    m_refLinkStage = RefLinkParserStage::S3;
                } else {
                    return returnWrong();
                }
            } break;

            // url
            case RefLinkParserStage::S3: {
                skipSpaces(currentLine);
                m_refLinkUrlPos.setStartColumn(currentLine.position());
                m_refLinkUrlPos.setStartLine(currentLine.lineNumber());

                const auto startUrlPos = currentLine.position();

                if (currentLine.position() == currentLine.length()) {
                    return RefLinkState::Continue;
                }

                if (!readLinkDestination(currentLine).isEmpty()) {
                    const auto endUrlPos = currentLine.position() - 1;

                    auto url = currentLine.slicedCopy(startUrlPos, endUrlPos - startUrlPos + 1);

                    m_refLinkUrlPos.setEndColumn(endUrlPos);
                    m_refLinkUrlPos.setEndLine(currentLine.lineNumber());

                    replaceEntity(url);
                    removeBackslashes(url);

                    if (url.startsWith(s_lessSignChar)) {
                        url.removeFirst();
                    }

                    if (url.endsWith(s_greaterSignChar)) {
                        url.removeLast();
                    }

                    if (!url.isEmpty()) {
                        if (QFileInfo::exists(url)) {
                            url = QFileInfo(url).absoluteFilePath();
                        } else if (QFileInfo::exists(path.isEmpty() ? QString() : path + s_solidusChar + url)) {
                            if (!path.isEmpty()) {
                                url = QFileInfo(path + s_solidusChar).absoluteFilePath() + url;
                            }
                        }
                    }

                    m_reference->setUrl(url);
                    m_reference->setUrlPos(m_refLinkUrlPos);
                    m_reference->setEndColumn(m_refLinkUrlPos.endColumn());
                    m_reference->setEndLine(m_refLinkUrlPos.endLine());
                    m_refLinkStage = RefLinkParserStage::S4;
                    m_wasSpace = currentLine.currentChar().isSpace() || currentLine.position() == currentLine.length();

                    if (currentLine.position() == currentLine.length()) {
                        m_startLine = currentLine.lineNumber() + 1;
                    }
                } else {
                    return returnWrong();
                }
            } break;

            // title
            case RefLinkParserStage::S4: {
                skipSpaces(currentLine);

                if (currentLine.position() == currentLine.length()
                    && m_refLinkUrlPos.startLine() == currentLine.lineNumber()) {
                    return RefLinkState::Continue;
                }

                if (!m_wasSpace) {
                    return returnWrong();
                }

                const auto titleStartPos = currentLine.position();

                bool endStarted = false;
                const auto title = readLinkTitle(currentLine,
                                                 m_refLinkTitleStartChar,
                                                 m_refLinkStartParenthesisCount,
                                                 m_refLinkTitleStartPos,
                                                 endStarted);
                if (!title.isEmpty()) {
                    if (!m_refLinkTitle.isEmpty()) {
                        m_refLinkTitle.append(s_newLineChar);
                    }
                    m_refLinkTitle.append(title);
                }

                if (m_refLinkTitlePos.startLine() == -1 && !m_refLinkTitleStartChar.isNull()) {
                    m_refLinkTitlePos.setStartColumn(titleStartPos);
                    m_refLinkTitlePos.setStartLine(currentLine.lineNumber());
                }

                if ((endStarted || (title.isEmpty() && !isEmptyLine(currentLine)))
                    && m_refLinkStartParenthesisCount
                    && currentLine.lineNumber() == m_refLinkUrlPos.endLine()) {
                    return returnWrong();
                }

                if (!m_refLinkStartParenthesisCount) {
                    if (m_refLinkTitleStartChar.isNull()
                        && currentLine.lineNumber() == m_refLinkUrlPos.endLine()
                        && currentLine.position() < currentLine.length()) {
                        return returnWrong();
                    }

                    if (!m_refLinkTitleStartChar.isNull()) {
                        m_refLinkTextPos.setStartColumn(m_refLinkTitleStartPos);
                        m_refLinkTitlePos.setEndColumn(currentLine.position() - 1);
                        m_refLinkTitlePos.setEndLine(currentLine.lineNumber());
                    }

                    m_refLinkStage = RefLinkParserStage::SF;

                    if (m_refLinkTitlePos.startColumn() != -1) {
                        makeReturnWrongOnFail = false;

                        skipSpaces(currentLine);

                        if (currentLine.position() != currentLine.length()) {
                            if (m_refLinkUrlPos.startLine() == m_refLinkTitlePos.startLine()) {
                                return returnWrong();
                            } else {
                                m_refLinkTitlePos = {};
                            }
                        } else {
                            m_startLine = currentLine.lineNumber() + 1;
                        }
                    }
                }
            } break;

            default: {
                if (m_refLinkTitlePos.startColumn() == -1) {
                    currentLine.restoreState(&st);
                    doBreak = true;
                }
            }
            }

            if (doBreak) {
                break;
            }
        } else {
            rs.next();
            currentLine.nextChar();
        }
    }

    switch (m_refLinkStage) {
    case RefLinkParserStage::SF: {
        insertRefLink(doc, currentLine.lineNumber() + 1);

        return (makeReturnWrongOnFail ? RefLinkState::Wrong : RefLinkState::Finished);
    }

    default:
        return RefLinkState::Continue;
    }
}

qsizetype ParagraphParser::insertRefLink(QSharedPointer<Document> doc,
                                         qsizetype pStartlineNumber)
{
    if (m_refLinkUrlPos.endLine() == m_refLinkTitlePos.startLine() && m_refLinkTitlePos.endLine() == -1) {
        return -1;
    }

    m_startLine = pStartlineNumber;

    m_reference->setText(m_refLinkLabel);
    m_reference->setTitle(m_refLinkTitle);
    m_reference->setTextPos(m_refLinkTextPos);

    if (m_refLinkTitlePos.endLine() != -1) {
        m_reference->setEndColumn(m_refLinkTitlePos.endColumn());
        m_reference->setEndLine(m_refLinkTitlePos.endLine());
    } else {
        m_startLine = m_refLinkUrlPos.endLine() + 1;
    }

    if (doc->labeledLinks().find(m_refLinkLabel) == doc->labeledLinks().cend()) {
        doc->insertLabeledLink(m_refLinkLabel, m_reference);
    }

    const auto lastLine = m_reference->endLine();

    clearRefLink();

    return lastLine;
}

BlockState ParagraphParser::check(Line &currentLine,
                                  TextStream &,
                                  QSharedPointer<Document> doc,
                                  Context &ctx,
                                  const QString &path,
                                  const QString &fileName,
                                  bool checkWithoutProcessing)
{
    currentLine.saveState();

    if (!isEmptyLine(currentLine)) {
        if (!checkWithoutProcessing) {
            ctx.appendLineInfo(currentLine.lineNumber(), currentLine.currentState());
            m_startLine = currentLine.lineNumber();

            if (checkForReferenceLink(currentLine, doc, ctx, path, fileName) == RefLinkState::Wrong) {
                ctx.setFirstLineNumber(currentLine.lineNumber());
                currentLine.skip();
            }

            ctx.setLastLineNumber(currentLine.lineNumber());
        } else {
            currentLine.restoreState();
        }

        return BlockState::Continue;
    }

    return BlockState::None;
}

BlockState ParagraphParser::continueCheck(Line &currentLine,
                                          TextStream &stream,
                                          QSharedPointer<Document> doc,
                                          Context &ctx,
                                          const QString &path,
                                          const QString &fileName)
{
    if (isEmptyLine(currentLine)) {
        if (m_reference && m_refLinkStage >= RefLinkParserStage::S4) {
            const auto lastRefLine = insertRefLink(doc, currentLine.lineNumber() + 1);

            if (ctx.firstLineNumber() == -1 && lastRefLine < currentLine.lineNumber() - 1) {
                ctx.setFirstLineNumber(m_startLine);
            }
        } else if (m_startLine < currentLine.lineNumber() && ctx.firstLineNumber() == -1) {
            ctx.setFirstLineNumber(m_startLine);
        }

        return BlockState::Stop;
    } else {
        auto block = parser()->checkBlockExcluding(currentLine, stream, doc, ctx, this);

        if (block
            && block->mayBreakParagraph(currentLine, stream, doc, ctx)
            && !ctx.isLazyLine(currentLine.lineNumber())) {
            if (currentLine.lineNumber() != m_startLine && ctx.firstLineNumber() == -1) {
                ctx.setFirstLineNumber(m_startLine);
            }

            return BlockState::Stop;
        } else {
            ctx.appendLineInfo(currentLine.lineNumber(), currentLine.currentState());

            if (checkForReferenceLink(currentLine, doc, ctx, path, fileName) == RefLinkState::Wrong) {
                if (ctx.firstLineNumber() == -1) {
                    ctx.setFirstLineNumber(m_startLine);
                }

                currentLine.skip();
            }

            ctx.setLastLineNumber(currentLine.lineNumber());

            return BlockState::Continue;
        }
    }
}

BlockState ParagraphParser::process(Line &currentLine,
                                    TextStream &stream,
                                    QSharedPointer<Document> doc,
                                    QSharedPointer<Block> parent,
                                    Context &ctx,
                                    const QString &path,
                                    const QString &fileName,
                                    QStringList &linksToParse)
{
    if (!m_finished) {
        if (ctx.startPos(currentLine.lineNumber()).m_pos == -1 && currentLine.lineNumber() >= ctx.firstLineNumber()) {
            if (ctx.firstLineNumber() != -1) {
                const auto mst = stream.currentState();

                InlineContext inlineContext;

                auto line = stream.moveTo(ctx.firstLineNumber());
                const auto st = ctx.startPos(line.lineNumber());
                line.restoreState(&st);

                m_paragraph = QSharedPointer<Paragraph>::create();
                m_paragraph->setStartColumn(line.position());
                m_paragraph->setStartLine(line.lineNumber());

                if (ctx.parent() && ctx.parent()->block() && ctx.parent()->item()) {
                    static_cast<Block *>(ctx.parent()->item())->appendItem(m_paragraph);
                } else {
                    parent->appendItem(m_paragraph);
                }

                ParagraphStream::HashedLines lines;
                const auto startLineNumber = line.lineNumber();
                auto endLineNumber = startLineNumber;

                while (line.lineNumber() < currentLine.lineNumber()) {
                    m_paragraph->setEndColumn(line.length() - 1);
                    m_paragraph->setEndLine(line.lineNumber());
                    lines.insert(line.lineNumber(), line);
                    endLineNumber = line.lineNumber();
                    line = stream.readLine();
                    const auto st = ctx.startPos(line.lineNumber());
                    line.restoreState(&st);
                }

                stream.restoreState(&mst);

                parser()->pushStateOfInliners();

                ParagraphStream pStream(lines, startLineNumber, endLineNumber);
                const auto pst = pStream.currentState();
                line = pStream.readLine();

                while (true) {
                    ReverseSolidusHandler rs;

                    while (line.position() < line.length()) {
                        auto processed = false;

                        rs.process(line.currentChar());

                        const auto parsers = parser()->inlineParsersFor(line.currentChar());

                        for (const auto &p : parsers) {
                            if (p->check(line,
                                         pStream,
                                         inlineContext,
                                         doc,
                                         path,
                                         fileName,
                                         linksToParse,
                                         *parser(),
                                         rs)) {
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

                    if (!pStream.atEnd()) {
                        line = pStream.readLine();
                    } else {
                        break;
                    }
                }

                pStream.restoreState(&pst);

                parser()->popStateOfInliners();

                EmphasisParser::processEmphasises(inlineContext);
                makeTextObjects(inlineContext, pStream, m_paragraph);
            }

            m_finished = true;

            return BlockState::Stop;
        } else {
            currentLine.skip();

            return BlockState::Continue;
        }
    }

    return BlockState::None;
}

void ParagraphParser::reset(Context &)
{
    resetOnAllContexts();
}

void ParagraphParser::resetOnAllContexts()
{
    m_paragraph.reset();
    m_finished = false;
    clearRefLink();
}

bool ParagraphParser::mayBreakParagraph(Line &,
                                        TextStream &,
                                        QSharedPointer<Document>,
                                        Context &) const
{
    return false;
}

bool ParagraphParser::canBeLazyLine(Line &,
                                    TextStream &,
                                    QSharedPointer<Document>,
                                    Context &) const
{
    return true;
}

void ParagraphParser::finish(Line &currentLine,
                             TextStream &stream,
                             QSharedPointer<Document> doc,
                             QSharedPointer<Block> parent,
                             Context &ctx,
                             const QString &path,
                             const QString &fileName,
                             QStringList &linksToParse)
{
    if (m_reference && m_refLinkStage >= RefLinkParserStage::S4) {
        insertRefLink(doc, -1);
    }

    if (parent) {
        process(currentLine, stream, doc, parent, ctx, path, fileName, linksToParse);
    }
}

inline qsizetype itemStartColumn(QSharedPointer<ItemWithOpts> item)
{
    if (item) {
        switch (item->type()) {
        case ItemType::Code:
        case ItemType::Math:
            return item.staticCast<Code>()->startDelim().startColumn();

        default:
            return item->startColumn();
        }
    } else {
        return -1;
    }
}

inline qsizetype itemEndColumn(QSharedPointer<ItemWithOpts> item)
{
    if (item) {
        switch (item->type()) {
        case ItemType::Code:
        case ItemType::Math:
            return item.staticCast<Code>()->endDelim().endColumn();

        default:
            return item->endColumn();
        }
    } else {
        return -1;
    }
}

inline qsizetype itemStartLine(QSharedPointer<ItemWithOpts> item)
{
    if (item) {
        switch (item->type()) {
        case ItemType::Code:
        case ItemType::Math:
            return item.staticCast<Code>()->startDelim().startLine();

        default:
            return item->startLine();
        }
    } else {
        return -1;
    }
}

inline qsizetype itemEndLine(QSharedPointer<ItemWithOpts> item)
{
    if (item) {
        switch (item->type()) {
        case ItemType::Code:
        case ItemType::Math:
            return item.staticCast<Code>()->endDelim().startLine();

        default:
            return item->endLine();
        }
    } else {
        return -1;
    }
}

struct ToPlace {
    StyleDelim m_styleDelim = StyleDelim(TextWithoutFormat, -1, -1, -1, -1);
    ItemWithOpts::Styles *m_styles = nullptr;
    QSharedPointer<ItemWithOpts> m_item;

    inline qsizetype startColumn() const
    {
        return (m_styleDelim.isNullPositions() ? itemStartColumn(m_item) : m_styleDelim.startColumn());
    }

    inline qsizetype endColumn() const
    {
        return (m_styleDelim.isNullPositions() ? itemEndColumn(m_item) : m_styleDelim.endColumn());
    }

    inline qsizetype startLine() const
    {
        return (m_styleDelim.isNullPositions() ? itemStartLine(m_item) : m_styleDelim.startLine());
    }

    inline qsizetype endLine() const
    {
        return (m_styleDelim.isNullPositions() ? itemEndLine(m_item) : m_styleDelim.endLine());
    }

    inline bool isNullPositions() const
    {
        return (m_styleDelim.isNullPositions() && itemStartLine(m_item) == -1);
    }
};

inline ToPlace firstToPlace(InlineContext &ctx,
                            const WithPosition &toSkip = {})
{
    ToPlace res;

    if (!ctx.openStyles().isEmpty()) {
        res.m_styleDelim = ctx.openStyles()[0];
        res.m_styles = &ctx.openStyles();
    }

    if (!ctx.closeStyles().isEmpty()) {
        if (ctx.closeStyles()[0] < res.m_styleDelim) {
            res.m_styleDelim = ctx.closeStyles()[0];
            res.m_styles = &ctx.closeStyles();
        }
    }

    if ((!toSkip.isNullPositions()
         && (toSkip.startLine() < res.startLine()
             || (toSkip.startLine() == res.startLine() && toSkip.startColumn() < res.startColumn())))
        || res.isNullPositions()) {
        res.m_styleDelim.applyPositions(toSkip);
        res.m_styles = nullptr;
    }

    if (!ctx.inlines().isEmpty()
        && ((ctx.inlines().front()->startLine() < res.startLine()
             || (ctx.inlines().front()->startLine() == res.startLine()
                 && ctx.inlines().front()->startColumn() < res.startColumn()))
            || res.startLine() == -1)) {
        res.m_styleDelim = StyleDelim(TextWithoutFormat, -1, -1, -1, -1);
        res.m_styles = nullptr;
        res.m_item = ctx.inlines().front();
        ctx.inlines().pop_front();
    }

    if (res.m_styles) {
        res.m_styles->removeFirst();
    }

    return res;
}

inline void moveAfterPos(Line &line,
                         ParagraphStream &stream,
                         const ToPlace &place)
{
    while (line.lineNumber() < place.endLine()) {
        line = stream.readLine();
    }

    while (line.position() <= place.endColumn()) {
        line.nextChar();
    }
}

inline void applyStyles(int &opts,
                        const ItemWithOpts::Styles &styles)
{
    for (const auto &s : styles) {
        opts |= s.style();
    }
}

inline void clearStyles(int &opts,
                        ItemWithOpts::Styles &opened)
{
    opts = TextWithoutFormat;

    opened.removeLast();

    applyStyles(opts, opened);
}

void ParagraphParser::makeTextObjects(InlineContext &ctx,
                                      ParagraphStream &stream,
                                      QSharedPointer<Block> p,
                                      const WithPosition &toSkip)
{
    stream.saveState();

    ItemWithOpts::Styles open;
    ItemWithOpts::Styles close;
    ItemWithOpts::Styles opened;

    int opts = TextWithoutFormat;
    ToPlace toPlace;

    auto placeEmph = [&]() {
        if (toPlace.m_styles) {
            if (toPlace.m_styles == &ctx.openStyles()) {
                open.append(toPlace.m_styleDelim);
                opened.append(toPlace.m_styleDelim);
                applyStyles(opts, open);
            } else {
                close.append(toPlace.m_styleDelim);
            }

            toPlace.m_styles = nullptr;
        }
    };

    QSharedPointer<ItemWithOpts> item;
    auto text = QSharedPointer<Text>::create();

    toPlace = firstToPlace(ctx, toSkip);

    while (!stream.atEnd()) {
        auto line = stream.readLine();
        const auto startPos = line.position();

        while (line.position() < line.length()) {
            if (!close.isEmpty() && !p->isEmpty()) {
                p->items().back().staticCast<ItemWithOpts>()->closeStyles().append(close);
                clearStyles(opts, opened);
                close.clear();
            }

            text->setOpts(opts);

            if (toPlace.m_item) {
                toPlace.m_item->setOpts(opts);
            }

            if (toPlace.startLine() == line.lineNumber()) {
                if (line.position() < toPlace.startColumn()) {
                    text->setStartColumn(line.position());
                    text->setStartLine(line.lineNumber());
                    text->setEndColumn(toPlace.startColumn() - 1);
                    text->setEndLine(line.lineNumber());

                    auto tmp = line.slicedCopy(line.position(), toPlace.startColumn() - line.position());

                    if (line.position() == startPos) {
                        const auto c = skipIf(0, tmp, [](const QChar &c) {
                            return c.isSpace();
                        });

                        tmp.remove(0, c);
                    }

                    replaceEntity(tmp);
                    removeBackslashes(tmp);

                    text->setText(tmp);

                    placeEmph();

                    if (!close.isEmpty()) {
                        text->closeStyles().append(close);
                        clearStyles(opts, opened);
                        close.clear();
                    }

                    if (!tmp.isEmpty()) {
                        p->appendItem(text);
                    }

                    item = nullptr;
                    text = QSharedPointer<Text>::create();
                }

                placeEmph();

                if (!open.isEmpty()) {
                    text->openStyles().append(open);

                    open.clear();
                }

                if (!close.isEmpty() && item) {
                    item->closeStyles().append(close);
                    clearStyles(opts, opened);
                    close.clear();
                }

                moveAfterPos(line, stream, toPlace);

                if (toPlace.m_item) {
                    toPlace.m_item->openStyles() = text->openStyles();
                    text->openStyles().clear();
                    p->appendItem(toPlace.m_item);
                    item = toPlace.m_item;
                }

                if (line.lineNumber() > toSkip.startLine()
                    || (line.lineNumber() == toSkip.startLine() && line.position() > toSkip.endColumn())) {
                    toPlace = firstToPlace(ctx, {});
                } else {
                    toPlace = firstToPlace(ctx, toSkip);
                }
            } else {
                text->setStartColumn(line.position());
                text->setStartLine(line.lineNumber());
                text->setEndColumn(line.length() - 1);
                text->setEndLine(line.lineNumber());

                auto tmp = line.slicedCopy(line.position());

                if (line.position() == startPos) {
                    const auto s = skipIf(0, tmp, [](const QChar &c) {
                        return c.isSpace();
                    });
                    tmp.remove(0, s);
                }

                const auto e = skipIfBackward(tmp.length() - 1, tmp, [](const QChar &c) {
                    return c.isSpace();
                });
                tmp.remove(e + 1, tmp.length() - e - 1);

                replaceEntity(tmp);
                removeBackslashes(tmp);

                text->setText(tmp);

                line.skip();

                if (!tmp.isEmpty()) {
                    p->appendItem(text);
                }

                item = nullptr;
                text = QSharedPointer<Text>::create();
            }
        }
    }

    if (!close.isEmpty() && !p->isEmpty()) {
        p->items().back().staticCast<ItemWithOpts>()->closeStyles().append(close);
    }

    stream.restoreState();
}

inline QString stringToLabel(const QString &s)
{
    QString res;

    for (long long int i = 0; i < s.length(); ++i) {
        const auto c = s[i];

        if (c.isLetter() || c.isDigit() || c == s_minusChar || c == s_lowLineChar) {
            res.push_back(c);
        } else if (c.isSpace()) {
            res.push_back(s_minusChar);
        }
    }

    return res;
}

QString ParagraphParser::paragraphToLabel(QSharedPointer<Paragraph> p)
{
    QString l;

    if (!p) {
        return l;
    }

    for (auto it = p->items().cbegin(), last = p->items().cend(); it != last; ++it) {
        switch ((*it)->type()) {
        case ItemType::Text: {
            auto t = static_cast<Text *>(it->get());
            const auto text = t->text();
            l.push_back(stringToLabel(text));
        } break;

        case ItemType::Image: {
            auto i = static_cast<Image *>(it->get());

            if (!i->p()->isEmpty()) {
                l.push_back(paragraphToLabel(i->p()));
            } else if (!i->text().isEmpty()) {
                l.push_back(stringToLabel(i->text()));
            }
        } break;

        case ItemType::Link: {
            auto link = static_cast<Link *>(it->get());

            if (!link->p()->isEmpty()) {
                l.push_back(paragraphToLabel(link->p()));
            } else if (!link->text().isEmpty()) {
                l.push_back(stringToLabel(link->text()));
            }
        } break;

        case ItemType::Code: {
            auto c = static_cast<Code *>(it->get());

            if (!c->text().isEmpty()) {
                l.push_back(stringToLabel(c->text()));
            }
        } break;

        default:
            break;
        }
    }

    return l;
}

} /* namespace MD */
