/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "inline_html_parser.h"
#include "constants.h"
#include "doc.h"
#include "inline_context.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

namespace MD
{

inline void makeInlineHtml(const QString &data,
                           qsizetype startPos,
                           qsizetype startLine,
                           qsizetype endPos,
                           qsizetype endLine,
                           InlineContext &ctx)
{
    auto html = QSharedPointer<RawHtml>::create();
    html->setStartColumn(startPos);
    html->setStartLine(startLine);
    html->setText(data);
    html->setEndColumn(endPos);
    html->setEndLine(endLine);

    ctx.inlines().append(html);
}

inline bool appendToHtml(QString &html,
                         qsizetype &startPos,
                         Line &line,
                         ParagraphStream &stream)
{
    html.append(line.slicedCopy(startPos));
    html.append(s_newLineChar);

    if (!stream.atEnd()) {
        line = stream.readLine();
        startPos = line.position();
        skipSpaces(line);

        return true;
    }

    return false;
}

inline void readAttrValue(qsizetype &startPos,
                          const QChar &ch,
                          Line &line,
                          ParagraphStream &stream,
                          int &stage,
                          int &nextStage,
                          bool &doBreak,
                          QString &html)
{
    auto localBreak = false;

    while (true) {
        while (line.position() < line.length()) {
            if (line.currentChar() == ch) {
                stage = 0;
                nextStage = 1;
                localBreak = true;
            }

            line.nextChar();

            if (localBreak) {
                break;
            }
        }

        if (localBreak) {
            break;
        }

        if (!appendToHtml(html, startPos, line, stream)) {
            doBreak = true;
            break;
        }
    }
}

inline bool pushIfClosed(const QString &html,
                         const QString &endString,
                         qsizetype startPos,
                         qsizetype startLine,
                         Line &line,
                         InlineContext &ctx)
{
    if (html.endsWith(endString)) {
        makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

        line.nextChar();

        return true;
    }

    return false;
}

InlineHtmlParser::InlineHtmlParser() = default;

InlineHtmlParser::~InlineHtmlParser() = default;

bool InlineHtmlParser::check(Line &line,
                             ParagraphStream &stream,
                             InlineContext &ctx,
                             QSharedPointer<Document>,
                             const QString &,
                             const QString &,
                             QStringList &,
                             Parser &,
                             const ReverseSolidusHandler &rs)
{
    const auto sState = stream.currentState();
    const auto lState = line.currentState();

    const auto returnWrong = [&]() -> bool {
        stream.restoreStateBefore(sState);
        line = stream.readLine();
        line.restoreState(&lState);

        return false;
    };

    if (line.currentChar() == s_lessSignChar && !rs.isPrevReverseSolidus()) {
        qsizetype startPos = line.position();
        qsizetype startLine = line.lineNumber();
        line.nextChar();

        QString html;
        QString tag;

        while (
            line.position() < line.length() && !line.currentChar().isSpace() && line.currentChar() != s_lessSignChar) {
            tag.append(line.currentChar());

            if (line.currentChar() == s_greaterSignChar) {
                break;
            }

            line.nextChar();
        }

        html = s_lessSignChar + tag;

        tag = tag.toLower();

        if (!tag.isEmpty()) {
            if (tag.startsWith(s_commentStartString)) {
                auto pos = line.position();

                if (pushIfClosed(html, s_commentEndString, startPos, startLine, line, ctx)) {
                    return true;
                }

                qsizetype count = 0;

                while (true) {
                    while (line.position() < line.length()) {
                        if (line.currentChar() == s_minusChar) {
                            ++count;
                        } else if (line.currentChar() == s_greaterSignChar) {
                            if (count > 1) {
                                html.append(line.slicedCopy(pos, line.position() - pos + 1));

                                makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

                                line.nextChar();

                                return true;
                            }

                            count = 0;
                        } else {
                            count = 0;
                        }

                        line.nextChar();
                    }

                    if (!appendToHtml(html, pos, line, stream)) {
                        break;
                    }
                }
            } else if (tag.startsWith(s_questionMarkChar)) {
                if (pushIfClosed(html, s_processInstructionEndString, startPos, startLine, line, ctx)) {
                    return true;
                }

                html.clear();
                auto pos = startPos;

                while (true) {
                    while (line.position() < line.length()) {
                        if (line.currentChar() == s_greaterSignChar && line.prevChar() == s_questionMarkChar) {
                            html.append(line.slicedCopy(pos, line.position() - pos + 1));

                            makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

                            line.nextChar();

                            return true;
                        }

                        line.nextChar();
                    }

                    if (!appendToHtml(html, pos, line, stream)) {
                        break;
                    }
                }
            } else if (tag.startsWith(s_cdataStartString)) {
                if (pushIfClosed(html, s_cdataEndString, startPos, startLine, line, ctx)) {
                    return true;
                }

                html.clear();
                auto pos = startPos;

                while (true) {
                    while (line.position() < line.length()) {
                        if (line.currentChar() == s_rightSquareBracketChar
                            && line.prevChar() == s_rightSquareBracketChar) {
                            line.nextChar();

                            if (line.currentChar() == s_greaterSignChar) {
                                html.append(line.slicedCopy(pos, line.position() - pos + 1));

                                makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

                                line.nextChar();

                                return true;
                            }
                        } else {
                            line.nextChar();
                        }
                    }

                    if (!appendToHtml(html, pos, line, stream)) {
                        break;
                    }
                }
            } else if (tag.startsWith(s_exclamationMarkChar)) {
                if (tag.length() > 1 && isAsciiLetter(tag[1])) {
                    if (pushIfClosed(html, QString(s_greaterSignChar), startPos, startLine, line, ctx)) {
                        return true;
                    }

                    html.clear();
                    auto pos = startPos;

                    while (true) {
                        while (line.position() < line.length()) {
                            if (line.currentChar() == s_greaterSignChar) {
                                html.append(line.slicedCopy(pos, line.position() - pos + 1));

                                makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

                                line.nextChar();

                                return true;
                            }

                            line.nextChar();
                        }

                        if (!appendToHtml(html, pos, line, stream)) {
                            break;
                        }
                    }
                }
            } else {
                bool closed = false;

                if (tag.endsWith(s_greaterSignChar)) {
                    closed = true;
                    tag.removeLast();
                }

                if (tag.startsWith(s_solidusChar)) {
                    if (!closed) {
                        const auto roll = line.makeRollbackObject();

                        skipSpaces(line);

                        if (line.currentChar() != s_greaterSignChar) {
                            return returnWrong();
                        }
                    }

                    tag.removeFirst();
                }

                if (tag.simplified().endsWith(s_solidusChar)) {
                    if (!closed || !tag.endsWith(s_solidusChar)) {
                        return returnWrong();
                    }

                    tag.removeLast();
                }

                if (isValidTagName(tag)) {
                    if (closed) {
                        makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

                        line.nextChar();

                        return true;
                    }

                    int stage = 0;
                    int nextStage = 1;

                    auto pos = line.position();
                    bool checkSpace = false;
                    bool spaceAfterAtrrName = false;

                    while (true) {
                        auto doBreak = false;

                        const auto checkClose = [&doBreak, &pos, &line, &html, &closed, &nextStage]() -> bool {
                            if (nextStage == 3) {
                                return false;
                            }

                            if (line.currentChar() == s_solidusChar) {
                                line.nextChar();

                                if (line.currentChar() != s_greaterSignChar) {
                                    doBreak = true;
                                    closed = false;

                                    return true;
                                }
                            }

                            if (line.currentChar() == s_greaterSignChar) {
                                html.append(line.slicedCopy(pos, line.position() - pos + 1));
                                closed = true;
                                doBreak = true;

                                return true;
                            }

                            return false;
                        };

                        switch (stage) {
                        case 0: {
                            if (!checkClose()) {
                                const auto p = line.position();

                                skipSpaces(line);

                                if (checkSpace) {
                                    checkSpace = false;

                                    if (p == line.position() && p < line.length()) {
                                        doBreak = true;
                                    }
                                }

                                if (line.position() == line.length() && !doBreak) {
                                    if (!appendToHtml(html, pos, line, stream)) {
                                        doBreak = true;
                                    } else {
                                        checkClose();
                                    }
                                }

                                stage = nextStage;
                            }
                        } break;

                        case 1: {
                            if (!checkClose()) {
                                if (!readHtmlAttrName(line)) {
                                    doBreak = true;
                                } else {
                                    stage = 0;
                                    nextStage = 2;
                                    spaceAfterAtrrName = line.currentChar().isSpace();
                                }
                            }
                        } break;

                        case 2: {
                            if (!checkClose()) {
                                if (line.currentChar() != s_equalSignChar) {
                                    stage = 0;
                                    nextStage = 1;

                                    if (!spaceAfterAtrrName) {
                                        doBreak = true;
                                    }
                                } else {
                                    line.nextChar();

                                    stage = 0;
                                    nextStage = 3;
                                }
                            }
                        } break;

                        case 3: {
                            if (line.currentChar() == s_quotationMarkChar) {
                                line.nextChar();
                                stage = 4;
                            } else if (line.currentChar() == s_apostropheChar) {
                                line.nextChar();
                                stage = 5;
                            } else {
                                stage = 6;
                            }
                        } break;

                        case 4: {
                            readAttrValue(pos, s_quotationMarkChar, line, stream, stage, nextStage, doBreak, html);
                            checkSpace = true;
                        } break;

                        case 5: {
                            readAttrValue(pos, s_apostropheChar, line, stream, stage, nextStage, doBreak, html);
                            checkSpace = true;
                        } break;

                        case 6: {
                            const auto startPos = line.position();

                            skipIf(line, [](const QChar &c) {
                                return (!c.isSpace()
                                        && c != s_quotationMarkChar
                                        && c != s_apostropheChar
                                        && c != s_equalSignChar
                                        && c != s_lessSignChar
                                        && c != s_greaterSignChar
                                        && c != s_graveAccentChar);
                            });

                            if (startPos == line.position()) {
                                doBreak = true;
                            } else {
                                stage = 0;
                                nextStage = 1;
                            }
                        } break;

                        // LCOV_EXCL_START
                        default: {
                            doBreak = true;
                        } break;
                            // LCOV_EXCL_STOP
                        }

                        if (doBreak) {
                            break;
                        }
                    }

                    if (closed) {
                        makeInlineHtml(html, startPos, startLine, line.position(), line.lineNumber(), ctx);

                        line.nextChar();

                        return true;
                    }
                }
            }
        }
    }

    return returnWrong();
}

QString InlineHtmlParser::startDelimiterSymbols() const
{
    return QStringLiteral("<");
}

} /* namespace MD */
