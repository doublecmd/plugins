/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "link_image_parser.h"
#include "constants.h"
#include "emphasis_parser.h"
#include "gfm_autolink_parser.h"
#include "inline_context.h"
#include "paragraph_parser.h"
#include "parser.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

// Qt include.
#include <QFileInfo>

namespace MD
{

qsizetype LinkImageParser::findOpener()
{
    qsizetype square = 0;

    for (qsizetype i = m_states.top().m_openers.size() - 1; i >= 0; --i) {
        if (m_states.top().m_openers[i].m_type == State::Delim::RightSquare) {
            ++square;
        } else if (m_states.top().m_openers[i].m_type == State::Delim::Link
                   || m_states.top().m_openers[i].m_type == State::Delim::Image) {
            if (!square && m_states.top().m_openers[i].m_active) {
                return i;
            } else {
                --square;
            }
        }
    }

    return -1;
}

void LinkImageParser::clearLinkOpenersAndCtx(qsizetype idx,
                                             const State::Delim &startDelim,
                                             const ParagraphStream::State &endStreamState,
                                             const Line::State &endLineState,
                                             InlineContext &ctx)
{
    if (m_states.top().m_openers[idx].m_type == State::Delim::Link) {
        for (qsizetype i = 0; i <= idx; ++i) {
            if (m_states.top().m_openers[i].m_type == State::Delim::Link) {
                m_states.top().m_openers[i].m_active = false;
            }
        }
    }

    m_states.top().m_openers.removeAt(idx);

    const WithPosition where{startDelim.m_lineState.m_pos,
                             startDelim.m_streamState.m_lineNumber - 1,
                             endLineState.m_pos - 1,
                             endStreamState.m_lineNumber - 1};

    for (qsizetype i = 0; i < m_states.top().m_openers.size(); ++i) {
        auto &o = m_states.top().m_openers[i];

        if (isIn(where,
                 {o.m_lineState.m_pos,
                  o.m_streamState.m_lineNumber - 1,
                  o.m_lineState.m_pos,
                  o.m_streamState.m_lineNumber - 1})) {
            m_states.top().m_openers.removeAt(i--);
        }
    }

    for (qsizetype i = 0; i < ctx.delims().size(); ++i) {
        auto &d = ctx.delims()[i];

        if (isIn(where, {d.m_pos, d.m_line, d.m_pos, d.m_line})) {
            ctx.delims().removeAt(i--);
        }
    }

    for (qsizetype i = 0; i < ctx.inlines().size() - 1; ++i) {
        auto &item = ctx.inlines()[i];

        if (isIn(where, *item)) {
            ctx.inlines().removeAt(i--);
        }
    }
}

QString LinkImageParser::prepareUrl(const QString &url,
                                    QStringList &linksToParse,
                                    const QString &path,
                                    const QString &fileName)
{
    QString u = url;

    if (!url.startsWith(s_numberSignChar)) {
        replaceEntity(u);
        removeBackslashes(u);
    }

    if (!u.isEmpty()) {
        if (!u.startsWith(s_numberSignChar)) {
            const auto checkForFile = [&](QString &url, const QString &ref = {}) -> bool {
                if (QFileInfo::exists(url)) {
                    url = QFileInfo(url).absoluteFilePath();

                    linksToParse.append(url);

                    if (!ref.isEmpty()) {
                        url = ref + s_solidusChar + url;
                    }

                    return true;
                } else if (QFileInfo::exists(path + s_solidusChar + url)) {
                    url = QFileInfo(path + s_solidusChar + url).absoluteFilePath();

                    linksToParse.append(url);

                    if (!ref.isEmpty()) {
                        url = ref + s_solidusChar + url;
                    }

                    return true;
                } else {
                    return false;
                }
            };

            if (!checkForFile(u) && u.contains(s_numberSignChar)) {
                const auto i = u.indexOf(s_numberSignChar);
                const auto ref = u.sliced(i);
                u = u.sliced(0, i);

                if (!checkForFile(u, ref)) {
                    u = u + ref;
                }
            }
        } else {
            u = u + (path.isEmpty() ? QString() : s_solidusChar + path) + s_solidusChar + fileName;

            linksToParse.append(u);
        }
    }

    return u;
}

QPair<QSharedPointer<Paragraph>,
      QString>
LinkImageParser::parseDescription(const State::Delim &startParagraphDelim,
                                  const State::Delim &endParagraphDelim,
                                  ParagraphStream &stream,
                                  Parser &parser,
                                  QSharedPointer<Document> doc,
                                  const QString &path,
                                  const QString &fileName,
                                  QStringList &linksToParse)
{
    const auto sState = stream.currentState();

    stream.restoreStateBefore(startParagraphDelim.m_streamState);
    auto line = stream.readLine();

    if (stream.currentState() == endParagraphDelim.m_streamState) {
        line = line.sliced(0, endParagraphDelim.m_lineState.m_pos + 1);
    } else if (!endParagraphDelim.m_streamState.m_lineNumber) {
        return qMakePair(QSharedPointer<Paragraph>::create(), QString());
    }

    line.restoreState(&startParagraphDelim.m_lineState);

    const auto startLine = line.lineNumber();
    auto endLine = startLine;

    auto paragraph = QSharedPointer<Paragraph>::create();
    paragraph->setStartColumn(line.position());
    paragraph->setStartLine(startLine);
    paragraph->setEndColumn(line.length() - 1);
    paragraph->setEndLine(endLine);

    QString text = line.slicedCopy(line.position());

    ParagraphStream::HashedLines lines;
    lines.insert(line.lineNumber(), line);

    while (stream.currentState() != endParagraphDelim.m_streamState) {
        line = stream.readLine();

        if (stream.currentState() == endParagraphDelim.m_streamState) {
            line = line.sliced(0, endParagraphDelim.m_lineState.m_pos + 1);
            lines.insert(line.lineNumber(), line);
            endLine = line.lineNumber();
            paragraph->setEndColumn(line.length() - 1);
            paragraph->setEndLine(endLine);
        } else {
            lines.insert(line.lineNumber(), line);
        }

        text.append(s_spaceChar);
        text.append(line.slicedCopy(line.position()));
    }

    parser.pushStateOfInliners();

    ParagraphStream pStream(lines, startLine, endLine);
    const auto pSState = pStream.currentState();
    InlineContext inlineContext;

    line = pStream.readLine();

    while (true) {
        ReverseSolidusHandler rs;

        while (line.position() < line.length()) {
            auto processed = false;

            rs.process(line.currentChar());

            const auto parsers = parser.inlineParsersFor(line.currentChar());

            for (const auto &p : parsers) {
                if (!p.dynamicCast<GfmAutolinkParser>()
                    && p->check(line, pStream, inlineContext, doc, path, fileName, linksToParse, parser, rs)) {
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

    parser.popStateOfInliners();

    pStream.restoreState(&pSState);

    EmphasisParser::processEmphasises(inlineContext);
    ParagraphParser::makeTextObjects(inlineContext, pStream, paragraph);

    stream.restoreState(&sState);

    return qMakePair(paragraph, text);
}

void LinkImageParser::setImgAndP(const QPair<QSharedPointer<Paragraph>,
                                             QString> &p,
                                 QSharedPointer<Link> link)
{
    if (!p.first->isEmpty()) {
        QSharedPointer<Image> img;

        for (auto it = p.first->items().cbegin(), last = p.first->items().cend(); it != last; ++it) {
            switch ((*it)->type()) {
            case ItemType::Image: {
                img = it->staticCast<Image>();
            } break;

            default:
                break;
            }

            if (img) {
                break;
            }
        }

        if (img) {
            link->setImg(img);
        }

        link->setP(p.first);
    }
}

void LinkImageParser::makeLink(const QString &url,
                               const QString &title,
                               QStringList &linksToParse,
                               const QString &path,
                               const QString &fileName,
                               const State::Delim &startParagraphDelim,
                               const State::Delim &endParagraphDelim,
                               ParagraphStream &stream,
                               InlineContext &ctx,
                               const WithPosition &textPos,
                               const WithPosition &urlPos,
                               Parser &parser,
                               QSharedPointer<Document> doc,
                               qsizetype startLinkPos,
                               qsizetype startLinkLine,
                               qsizetype endLinkPos,
                               qsizetype endLinkLine)
{
    auto link = QSharedPointer<Link>::create();

    link->setUrl(prepareUrl(url, linksToParse, path, fileName));
    link->setTitle(title);
    link->setTextPos(textPos);
    link->setUrlPos(urlPos);

    const auto sState = stream.currentState();

    auto p =
        parseDescription(startParagraphDelim, endParagraphDelim, stream, parser, doc, path, fileName, linksToParse);

    setImgAndP(p, link);

    removeBackslashes(p.second);

    link->setText(p.second);
    link->setStartColumn(startLinkPos);
    link->setStartLine(startLinkLine);
    link->setEndColumn(endLinkPos);
    link->setEndLine(endLinkLine);

    stream.restoreState(&sState);

    ctx.inlines().append(link);
}

void LinkImageParser::makeImage(const QString &url,
                                const QString &title,
                                QStringList &linksToParse,
                                const QString &path,
                                const QString &fileName,
                                const State::Delim &startParagraphDelim,
                                const State::Delim &endParagraphDelim,
                                ParagraphStream &stream,
                                InlineContext &ctx,
                                const WithPosition &textPos,
                                const WithPosition &urlPos,
                                Parser &parser,
                                QSharedPointer<Document> doc,
                                qsizetype startLinkPos,
                                qsizetype startLinkLine,
                                qsizetype endLinkPos,
                                qsizetype endLinkLine)
{
    auto img = QSharedPointer<Image>::create();

    img->setUrl(prepareUrl(url, linksToParse, path, fileName));
    img->setTitle(title);
    img->setTextPos(textPos);
    img->setUrlPos(urlPos);

    const auto sState = stream.currentState();

    auto p =
        parseDescription(startParagraphDelim, endParagraphDelim, stream, parser, doc, path, fileName, linksToParse);

    removeBackslashes(p.second);

    img->setP(p.first);
    img->setText(p.second.trimmed());
    img->setStartColumn(startLinkPos);
    img->setStartLine(startLinkLine);
    img->setEndColumn(endLinkPos);
    img->setEndLine(endLinkLine);

    stream.restoreState(&sState);

    ctx.inlines().append(img);
}

QString LinkImageParser::readLabel(const State::Delim &startDelim,
                                   const ParagraphStream::State &endStreamState,
                                   const Line::State &endLineState,
                                   ParagraphStream &stream,
                                   qsizetype &startLabelPos,
                                   qsizetype &startLabelLine,
                                   qsizetype &endLabelPos,
                                   qsizetype &endLabelLine,
                                   State::Delim &startParagraphDelim,
                                   State::Delim &endParagraphDelim,
                                   qsizetype &startTextPos,
                                   qsizetype &startTextLine,
                                   qsizetype &endTextPos,
                                   qsizetype &endTextLine)
{
    const auto sState = stream.currentState();

    stream.restoreStateBefore(startDelim.m_streamState);
    auto line = stream.readLine();
    line.restoreState(&startDelim.m_lineState);

    startLabelLine = line.lineNumber();
    endLabelPos = endLineState.m_pos;
    endLabelLine = endStreamState.m_lineNumber - 1;
    startLabelPos = line.position();

    auto pos = line.position() + (startDelim.m_type == State::Delim::Link ? 1 : 2);

    startParagraphDelim.m_type = State::Delim::Link;

    startTextPos = -1;
    startTextLine = -1;
    endTextPos = endLineState.m_pos - 1;
    endTextLine = endStreamState.m_lineNumber - 1;

    if (pos < line.length()) {
        startTextPos = startDelim.m_lineState.m_pos + (startDelim.m_type == State::Delim::Link ? 1 : 2);
        startParagraphDelim.m_lineState =
            Line::State{startDelim.m_lineState.m_column + (startDelim.m_type == State::Delim::Link ? 1 : 2),
                        startTextPos};
        startParagraphDelim.m_streamState = startDelim.m_streamState;
        startTextLine = startDelim.m_streamState.m_lineNumber - 1;
    } else {
        line = stream.readLine();
        startTextPos = line.position();
        startParagraphDelim.m_lineState = line.currentState();
        startParagraphDelim.m_streamState = stream.currentState();
        startTextLine = line.lineNumber();
        pos = startTextPos;
    }

    endParagraphDelim.m_type = State::Delim::Link;

    QString label;

    while (true) {
        QString tmp;

        if (stream.currentState() == endStreamState) {
            tmp = line.slicedCopy(pos, endLineState.m_pos - pos);
            label.append(tmp);

            if (!tmp.isEmpty()) {
                endParagraphDelim.m_lineState = Line::State{endLineState.m_column - 1, endLineState.m_pos - 1};
                endParagraphDelim.m_streamState = endStreamState;
                endTextPos = endLineState.m_pos - 1;
                endTextLine = line.lineNumber();
            }
        } else {
            tmp = line.slicedCopy(pos);
            label.append(tmp);

            if (!tmp.isEmpty()) {
                endParagraphDelim.m_lineState = Line::State{line.length() - 1, line.length() - 1};
                endParagraphDelim.m_streamState = stream.currentState();
                endTextPos = line.length() - 1;
                endTextLine = line.lineNumber();
            }
        }

        label.append(s_spaceChar);

        if (stream.currentState() != endStreamState) {
            line = stream.readLine();
            pos = line.position();
        } else {
            break;
        }
    }

    stream.restoreState(&sState);

    return label;
}

QPair<bool,
      bool>
LinkImageParser::checkShortcutLinkImage(const State::Delim &startDelim,
                                        const ParagraphStream::State &endStreamState,
                                        const Line::State &endLineState,
                                        ParagraphStream &stream,
                                        InlineContext &ctx,
                                        QSharedPointer<Document> doc,
                                        const QString &path,
                                        const QString &fileName,
                                        QStringList &linksToParse,
                                        Parser &parser)
{
    const auto startState = stream.currentState();

    qsizetype startLinkLine;
    qsizetype endLinkPos;
    qsizetype endLinkLine;
    qsizetype startLinkPos;
    State::Delim startParagraphDelim;
    State::Delim endParagraphDelim;
    qsizetype startTextPos;
    qsizetype startTextLine;
    qsizetype endTextPos;
    qsizetype endTextLine;

    const auto label = readLabel(startDelim,
                                 endStreamState,
                                 endLineState,
                                 stream,
                                 startLinkPos,
                                 startLinkLine,
                                 endLinkPos,
                                 endLinkLine,
                                 startParagraphDelim,
                                 endParagraphDelim,
                                 startTextPos,
                                 startTextLine,
                                 endTextPos,
                                 endTextLine)
                           .simplified();

    if (!label.isEmpty()) {
        const QString u = s_numberSignChar + label.toCaseFolded().toUpper();
        const QString url = u + s_solidusChar + (path.isEmpty() ? QString() : path + s_solidusChar) + fileName;

        const auto it = doc->labeledLinks().find(url);

        if (it != doc->labeledLinks().cend()) {
            if (startDelim.m_type == State::Delim::Link) {
                makeLink(u,
                         it.value()->title(),
                         linksToParse,
                         path,
                         fileName,
                         startParagraphDelim,
                         endParagraphDelim,
                         stream,
                         ctx,
                         WithPosition{startTextPos, startTextLine, endTextPos, endTextLine},
                         WithPosition{startTextPos, startTextLine, endTextPos, endTextLine},
                         parser,
                         doc,
                         startLinkPos,
                         startLinkLine,
                         endLinkPos,
                         endLinkLine);
            } else {
                makeImage(it.value()->url(),
                          it.value()->title(),
                          linksToParse,
                          path,
                          fileName,
                          startParagraphDelim,
                          endParagraphDelim,
                          stream,
                          ctx,
                          WithPosition{startTextPos, startTextLine, endTextPos, endTextLine},
                          WithPosition{startTextPos, startTextLine, endTextPos, endTextLine},
                          parser,
                          doc,
                          startLinkPos,
                          startLinkLine,
                          endLinkPos,
                          endLinkLine);
            }

            return qMakePair(true, false);
        } else if (startDelim.m_type == State::Delim::Link) {
            const auto it = doc->footnotesMap().find(url);

            if (it != doc->footnotesMap().cend()) {
                auto ref = QSharedPointer<FootnoteRef>::create(url);

                ref->setIdPos(WithPosition{startTextPos, startTextLine, endTextPos, endTextLine});
                ref->setStartColumn(startLinkPos);
                ref->setStartLine(startLinkLine);
                ref->setEndColumn(endLinkPos);
                ref->setEndLine(endLinkLine);

                ctx.inlines().append(ref);

                return qMakePair(true, true);
            }
        }
    }

    stream.restoreState(&startState);

    return qMakePair(false, false);
}

QPair<QString,
      bool>
readLinkTitle(Line &line,
              ParagraphStream &stream,
              bool space)
{
    if (!space) {
        return qMakePair(QString(), line.currentChar() == s_rightParenthesisChar);
    }

    if (line.currentChar() != s_quotationMarkChar
        && line.currentChar() != s_apostropheChar
        && line.currentChar() != s_leftParenthesisChar) {
        return qMakePair(QString(), line.currentChar() == s_rightParenthesisChar);
    }

    const auto sc = line.currentChar();
    auto lp = line.position();

    if (line.currentChar() == s_leftParenthesisChar) {
        skipIf(line, [](const QChar &c) {
            return c == s_leftParenthesisChar;
        });

        lp = line.position() - lp;
    } else {
        lp = 0;
        line.nextChar();
    }

    QString title;

    while (true) {
        ReverseSolidusHandler reverseSolidus;

        while (line.position() < line.length()) {
            if (reverseSolidus.process(line.currentChar())) {
            } else if (lp && line.currentChar() == s_rightParenthesisChar && !reverseSolidus.isPrevReverseSolidus()) {
                --lp;

                if (!lp) {
                    line.nextChar();
                    return qMakePair(title, true);
                }
            } else if (sc == s_leftParenthesisChar
                       && line.currentChar() == s_leftParenthesisChar
                       && !reverseSolidus.isPrevReverseSolidus()) {
                return qMakePair(QString(), false);
            } else if (sc != s_leftParenthesisChar
                       && line.currentChar() == sc
                       && !reverseSolidus.isPrevReverseSolidus()) {
                line.nextChar();
                return qMakePair(title, true);
            } else {
                title.append(line.currentChar());
            }

            line.nextChar();
            reverseSolidus.next();
        }

        if (!stream.atEnd()) {
            line = stream.readLine();
            title.append(s_spaceChar);
        } else {
            break;
        }
    }

    return qMakePair(QString(), false);
}

bool LinkImageParser::checkInlineLinkImage(const State::Delim &startDelim,
                                           const ParagraphStream::State &endDescStreamState,
                                           const Line::State &endDescLineState,
                                           const ParagraphStream::State &,
                                           const Line::State &,
                                           ParagraphStream &stream,
                                           InlineContext &ctx,
                                           QSharedPointer<Document> doc,
                                           const QString &path,
                                           const QString &fileName,
                                           QStringList &linksToParse,
                                           Parser &parser)
{
    const auto startState = stream.currentState();

    stream.restoreStateBefore(endDescStreamState);
    auto line = stream.readLine();
    line.restoreState(&endDescLineState);

    const auto startLinkLine = startDelim.m_streamState.m_lineNumber - 1;
    const auto startLinkPos = startDelim.m_lineState.m_pos;

    line.nextChar();
    line.nextChar();

    if (line.position() >= line.length()) {
        line = stream.readLine();
    }

    skipSpaces(line);

    const auto startUrlPos = line.position();
    const auto startUrlLine = line.lineNumber();

    if (!readLinkDestination(line).isEmpty()) {
        const auto endUrlPos = line.position() - 1;

        auto url = line.slicedCopy(startUrlPos, endUrlPos - startUrlPos + 1);

        if (url.startsWith(s_lessSignChar)) {
            url.removeFirst();
            url.removeLast();
        }

        auto space = line.currentChar().isSpace();

        skipSpaces(line);

        if (line.position() >= line.length()) {
            line = stream.readLine();
            skipSpaces(line);
            space = true;
        }

        const auto title = readLinkTitle(line, stream, space);

        if (title.second) {
            if (line.currentChar() != s_rightParenthesisChar) {
                skipSpaces(line);

                if (line.position() >= line.length()) {
                    line = stream.readLine();
                    skipSpaces(line);
                }
            }

            if (line.currentChar() == s_rightParenthesisChar) {
                qsizetype startLabelLine;
                qsizetype endLabelPos;
                qsizetype endLabelLine;
                qsizetype startLabelPos;
                State::Delim startParagraphDelim;
                State::Delim endParagraphDelim;
                qsizetype startTextPos;
                qsizetype startTextLine;
                qsizetype endTextPos;
                qsizetype endTextLine;

                readLabel(startDelim,
                          endDescStreamState,
                          endDescLineState,
                          stream,
                          startLabelPos,
                          startLabelLine,
                          endLabelPos,
                          endLabelLine,
                          startParagraphDelim,
                          endParagraphDelim,
                          startTextPos,
                          startTextLine,
                          endTextPos,
                          endTextLine);

                if (startDelim.m_type == State::Delim::Link) {
                    makeLink(url,
                             title.first,
                             linksToParse,
                             path,
                             fileName,
                             startParagraphDelim,
                             endParagraphDelim,
                             stream,
                             ctx,
                             {startTextPos, startTextLine, endTextPos, endTextLine},
                             {startUrlPos, startUrlLine, endUrlPos, startUrlLine},
                             parser,
                             doc,
                             startLinkPos,
                             startLinkLine,
                             line.position(),
                             line.lineNumber());
                } else {
                    makeImage(url,
                              title.first,
                              linksToParse,
                              path,
                              fileName,
                              startParagraphDelim,
                              endParagraphDelim,
                              stream,
                              ctx,
                              {startTextPos, startTextLine, endTextPos, endTextLine},
                              {startUrlPos, startUrlLine, endUrlPos, startUrlLine},
                              parser,
                              doc,
                              startLinkPos,
                              startLinkLine,
                              line.position(),
                              line.lineNumber());
                }

                return true;
            }
        }
    }

    stream.restoreState(&startState);

    return false;
}

bool LinkImageParser::checkRefLinkImage(const State::Delim &startDelim,
                                        const ParagraphStream::State &endDescStreamState,
                                        const Line::State &endDescLineState,
                                        const ParagraphStream::State &endStreamState,
                                        const Line::State &endLineState,
                                        ParagraphStream &stream,
                                        InlineContext &ctx,
                                        QSharedPointer<Document> doc,
                                        const QString &path,
                                        const QString &fileName,
                                        QStringList &linksToParse,
                                        Parser &parser)
{
    const auto startState = stream.currentState();

    stream.restoreStateBefore(endDescStreamState);
    auto line = stream.readLine();
    line.restoreState(&endDescLineState);

    const auto st = line.currentState();

    line.nextChar();

    const auto refStartLineState = line.currentState();

    line.nextChar();

    if (line.currentChar() == s_rightSquareBracketChar) {
        const auto res = checkShortcutLinkImage(startDelim,
                                                endDescStreamState,
                                                endDescLineState,
                                                stream,
                                                ctx,
                                                doc,
                                                path,
                                                fileName,
                                                linksToParse,
                                                parser);

        if (res.first) {
            if (!res.second) {
                ctx.inlines().back()->setEndColumn(line.position());
            } else {
                line.restoreState(&st);
            }

            return true;
        }
    }

    auto pos = line.position();

    if (pos >= line.length()) {
        line = stream.readLine();
    }

    skipSpaces(line);

    State::Delim refDelim;
    refDelim.m_type = State::Delim::Link;
    refDelim.m_streamState = endDescStreamState;
    refDelim.m_lineState = refStartLineState;

    const auto res = checkShortcutLinkImage(refDelim,
                                            endStreamState,
                                            endLineState,
                                            stream,
                                            ctx,
                                            doc,
                                            path,
                                            fileName,
                                            linksToParse,
                                            parser);

    if (res.first) {
        if (!res.second) {
            qsizetype startLabelPos;
            qsizetype startLabelLine;
            qsizetype endLabelPos;
            qsizetype endLabelLine;
            State::Delim startParagraphDelim;
            State::Delim endParagraphDelim;
            qsizetype startTextPos;
            qsizetype startTextLine;
            qsizetype endTextPos;
            qsizetype endTextLine;

            readLabel(startDelim,
                      endDescStreamState,
                      endDescLineState,
                      stream,
                      startLabelPos,
                      startLabelLine,
                      endLabelPos,
                      endLabelLine,
                      startParagraphDelim,
                      endParagraphDelim,
                      startTextPos,
                      startTextLine,
                      endTextPos,
                      endTextLine);
            auto p = parseDescription(startParagraphDelim,
                                      endParagraphDelim,
                                      stream,
                                      parser,
                                      doc,
                                      path,
                                      fileName,
                                      linksToParse);

            auto link = ctx.inlines().back().staticCast<Link>();
            ctx.inlines().pop_back();

            if (startDelim.m_type == State::Delim::Link) {
                link->setStartColumn(startLabelPos);
                link->setStartLine(startLabelLine);
                link->setUrlPos(link->textPos());
                link->setTextPos({startTextPos, startTextLine, endTextPos, endTextLine});
                setImgAndP(p, link);
                removeBackslashes(p.second);
                link->setText(p.second);
                ctx.inlines().append(link);
            } else {
                auto img = QSharedPointer<Image>::create();
                const auto it = doc->labeledLinks().find(link->url());

                if (it != doc->labeledLinks().cend()) {
                    img->setUrl(it.value()->url());
                    img->setTitle(it.value()->title());
                }

                img->setStartColumn(startLabelPos);
                img->setStartLine(startLabelLine);
                img->setEndColumn(link->endColumn());
                img->setEndLine(link->endLine());
                img->setText(p.second);
                img->setP(p.first);
                img->setUrlPos(link->textPos());
                img->setTextPos({startTextPos, startTextLine, endTextPos, endTextLine});
                ctx.inlines().append(img);
            }

            return true;
        }
    }

    stream.restoreState(&startState);

    return false;
}

LinkImageParser::LinkImageParser() = default;

LinkImageParser::~LinkImageParser() = default;

bool LinkImageParser::check(Line &line,
                            ParagraphStream &stream,
                            InlineContext &ctx,
                            QSharedPointer<Document> doc,
                            const QString &path,
                            const QString &fileName,
                            QStringList &linksToParse,
                            Parser &parser,
                            const ReverseSolidusHandler &rs)
{
    if (!rs.isPrevReverseSolidus()) {
        if (line.currentChar() == s_exclamationMarkChar) {
            const auto st = line.currentState();
            line.nextChar();

            if (line.currentChar() == s_leftSquareBracketChar) {
                m_states.top().m_openers.append({stream.currentState(), st, State::Delim::Image});
            } else {
                line.restoreState(&st);
            }
        } else if (line.currentChar() == s_leftSquareBracketChar) {
            m_states.top().m_openers.append({stream.currentState(), line.currentState(), State::Delim::Link});
        } else if (line.currentChar() == s_rightSquareBracketChar) {
            const auto openerIdx = findOpener();

            if (openerIdx != -1) {
                const auto startDelim = m_states.top().m_openers[openerIdx];

                const auto sState = stream.currentState();
                const auto lState = line.currentState();

                line.nextChar();

                const auto sStateDestStart = stream.currentState();
                const auto lStateDestStart = line.currentState();

                if (line.currentChar() == s_leftParenthesisChar) {
                    qsizetype par = 0;

                    line.nextChar();

                    skipSpaces(line);

                    auto checkParenthesis = line.currentChar() != s_lessSignChar;

                    while (true) {
                        ReverseSolidusHandler rs;

                        while (line.position() < line.length()) {
                            if (rs.isNotEscaped(line.currentChar())) {
                                if (line.currentChar() == s_greaterSignChar) {
                                    if (!checkParenthesis) {
                                        checkParenthesis = true;
                                    }
                                } else if (line.currentChar() == s_leftParenthesisChar) {
                                    if (checkParenthesis) {
                                        ++par;
                                    }
                                } else if (line.currentChar() == s_rightParenthesisChar) {
                                    if (par) {
                                        --par;
                                    } else {
                                        if (checkInlineLinkImage(startDelim,
                                                                 sState,
                                                                 lState,
                                                                 stream.currentState(),
                                                                 line.currentState(),
                                                                 stream,
                                                                 ctx,
                                                                 doc,
                                                                 path,
                                                                 fileName,
                                                                 linksToParse,
                                                                 parser)) {
                                            line.nextChar();
                                            clearLinkOpenersAndCtx(openerIdx,
                                                                   startDelim,
                                                                   stream.currentState(),
                                                                   line.currentState(),
                                                                   ctx);
                                            return true;
                                        }
                                    }
                                }
                            }

                            line.nextChar();
                            rs.next();
                        }

                        if (stream.atEnd()) {
                            break;
                        }

                        line = stream.readLine();
                    }

                    const auto res = checkShortcutLinkImage(startDelim,
                                                            sState,
                                                            lState,
                                                            stream,
                                                            ctx,
                                                            doc,
                                                            path,
                                                            fileName,
                                                            linksToParse,
                                                            parser);

                    if (!res.first) {
                        stream.restoreStateBefore(sState);
                        line = stream.readLine();
                        line.restoreState(&lState);

                        m_states.top().m_openers.append(
                            {stream.currentState(), line.currentState(), State::Delim::RightSquare});
                    } else {
                        stream.restoreStateBefore(sStateDestStart);
                        line = stream.readLine();
                        line.restoreState(&lStateDestStart);
                        clearLinkOpenersAndCtx(openerIdx, startDelim, sStateDestStart, lStateDestStart, ctx);
                        return true;
                    }
                } else if (line.currentChar() == s_leftSquareBracketChar) {
                    const auto tmpSState = stream.currentState();
                    const auto tmpLState = line.currentState();
                    line.nextChar();
                    auto stop = false;

                    while (true) {
                        ReverseSolidusHandler rs;

                        while (line.position() < line.length()) {
                            if (rs.isNotEscaped(line.currentChar())) {
                                if (line.currentChar() == s_leftSquareBracketChar) {
                                    stop = true;
                                    break;
                                } else if (line.currentChar() == s_rightSquareBracketChar) {
                                    if (checkRefLinkImage(startDelim,
                                                          sState,
                                                          lState,
                                                          stream.currentState(),
                                                          line.currentState(),
                                                          stream,
                                                          ctx,
                                                          doc,
                                                          path,
                                                          fileName,
                                                          linksToParse,
                                                          parser)) {
                                        line.nextChar();
                                        clearLinkOpenersAndCtx(openerIdx,
                                                               startDelim,
                                                               stream.currentState(),
                                                               line.currentState(),
                                                               ctx);
                                        return true;
                                    } else {
                                        stream.restoreStateBefore(sState);
                                        line = stream.readLine();
                                        line.restoreState(&lState);

                                        m_states.top().m_openers.append({sState, lState, State::Delim::RightSquare});

                                        m_states.top().m_openers.append({tmpSState, tmpLState, State::Delim::Link});

                                        return false;
                                    }
                                }
                            }

                            line.nextChar();
                            rs.next();
                        }

                        if (stream.atEnd() || stop) {
                            break;
                        }

                        line = stream.readLine();
                    }

                    const auto res = checkShortcutLinkImage(startDelim,
                                                            sState,
                                                            lState,
                                                            stream,
                                                            ctx,
                                                            doc,
                                                            path,
                                                            fileName,
                                                            linksToParse,
                                                            parser);

                    if (!res.first) {
                        stream.restoreStateBefore(sState);
                        line = stream.readLine();
                        line.restoreState(&lState);

                        m_states.top().m_openers.append(
                            {stream.currentState(), line.currentState(), State::Delim::RightSquare});
                    } else {
                        stream.restoreStateBefore(sStateDestStart);
                        line = stream.readLine();
                        line.restoreState(&lStateDestStart);
                        clearLinkOpenersAndCtx(openerIdx, startDelim, sStateDestStart, lStateDestStart, ctx);
                        return true;
                    }
                } else {
                    const auto res = checkShortcutLinkImage(startDelim,
                                                            sState,
                                                            lState,
                                                            stream,
                                                            ctx,
                                                            doc,
                                                            path,
                                                            fileName,
                                                            linksToParse,
                                                            parser);

                    if (!res.first) {
                        stream.restoreStateBefore(sState);
                        line = stream.readLine();
                        line.restoreState(&lState);

                        m_states.top().m_openers.append(
                            {stream.currentState(), line.currentState(), State::Delim::RightSquare});
                    } else {
                        stream.restoreStateBefore(sStateDestStart);
                        line = stream.readLine();
                        line.restoreState(&lStateDestStart);
                        clearLinkOpenersAndCtx(openerIdx, startDelim, sStateDestStart, lStateDestStart, ctx);
                        return true;
                    }
                }
            } else {
                m_states.top().m_openers.append(
                    {stream.currentState(), line.currentState(), State::Delim::RightSquare});
            }
        }
    }

    return false;
}

void LinkImageParser::pushState()
{
    m_states.push({});
}

void LinkImageParser::popState()
{
    m_states.pop();
}

QString LinkImageParser::startDelimiterSymbols() const
{
    return QStringLiteral("![]");
}

} /* namespace MD */
