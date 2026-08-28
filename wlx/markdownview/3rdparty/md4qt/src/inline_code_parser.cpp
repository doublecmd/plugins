/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "inline_code_parser.h"
#include "constants.h"
#include "doc.h"
#include "inline_context.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

namespace MD
{

InlineCodeParser::InlineCodeParser() = default;

InlineCodeParser::~InlineCodeParser() = default;

bool InlineCodeParser::check(Line &line,
                             ParagraphStream &stream,
                             InlineContext &ctx,
                             QSharedPointer<Document>,
                             const QString &,
                             const QString &,
                             QStringList &,
                             Parser &,
                             const ReverseSolidusHandler &rs)
{
    if (line.currentChar() == s_graveAccentChar && !rs.isPrevReverseSolidus()) {
        const auto startPos = line.position();
        Line::State lState;

        skipIf(line, [&lState, &line](const QChar &c) {
            if (c == s_graveAccentChar) {
                lState = line.currentState();

                return true;
            } else {
                return false;
            }
        });

        const auto sState = stream.currentState();

        const auto endPos = line.position();
        auto startCodePos = endPos;
        auto startCodeLine = line.lineNumber();
        auto endCodePos = startCodePos;
        auto endCodeLine = startCodeLine;

        QString code;

        auto tmp = line;
        auto end = endPos;

        while (true) {
            while (tmp.position() < tmp.length()) {
                if (startCodePos == -1) {
                    startCodePos = tmp.position();
                }

                skipIf(tmp, [](const QChar &c) {
                    return c != s_graveAccentChar;
                });
                const auto tmpStartPos = tmp.position();

                if (tmp.position() < tmp.length()) {
                    skipIf(tmp, [](const QChar &c) {
                        return c == s_graveAccentChar;
                    });

                    const auto tmpEndPos = tmp.position();

                    if (endPos - startPos == tmpEndPos - tmpStartPos) {
                        if (tmpStartPos - end) {
                            endCodePos = tmpStartPos - 1;
                            endCodeLine = tmp.lineNumber();
                        }

                        if (!code.isEmpty() && tmpStartPos - end > 0) {
                            code.append(s_spaceChar);
                        }

                        code.append(tmp.slicedCopy(end, tmpStartPos - end));

                        if (code.startsWith(s_spaceChar)
                            && code.endsWith(s_spaceChar)
                            && !code.simplified().isEmpty()) {
                            code.removeFirst();
                            code.removeLast();
                        }

                        auto item = QSharedPointer<Code>::create(code, false, true);
                        item->setStartColumn(startCodePos);
                        item->setStartLine(startCodeLine);
                        item->setEndColumn(endCodePos);
                        item->setEndLine(endCodeLine);
                        item->setStartDelim({startPos, line.lineNumber(), endPos - 1, line.lineNumber()});
                        item->setEndDelim({tmpStartPos, tmp.lineNumber(), tmpEndPos - 1, tmp.lineNumber()});

                        ctx.inlines().append(item);

                        line = tmp;

                        return true;
                    }
                }
            }

            if (end < tmp.length()) {
                if (!code.isEmpty()) {
                    code.append(s_spaceChar);
                }

                code.append(tmp.slicedCopy(end));

                endCodePos = tmp.length() - 1;
                endCodeLine = tmp.lineNumber();
            } else if (tmp.lineNumber() == startCodeLine) {
                startCodeLine = tmp.lineNumber() + 1;
                startCodePos = -1;
                endCodeLine = startCodeLine;
            }

            if (stream.atEnd()) {
                break;
            }

            tmp = stream.readLine();

            end = tmp.position();
        }

        stream.restoreStateBefore(sState);
        line = stream.readLine();
        line.restoreState(&lState);
    }

    return false;
}

QString InlineCodeParser::startDelimiterSymbols() const
{
    return QStringLiteral("`");
}

} /* namespace MD */
