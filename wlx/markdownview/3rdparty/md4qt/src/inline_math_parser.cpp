/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "inline_math_parser.h"
#include "doc.h"
#include "inline_context.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

namespace MD
{

inline void appendCode(const Line &line,
                       qsizetype start,
                       qsizetype end,
                       QString &code,
                       bool updatePos,
                       qsizetype &startPos,
                       qsizetype &startLine)
{
    if (start < end) {
        if (!code.isEmpty()) {
            code.append(s_newLineChar);
        }

        code.append(line.slicedCopy(start, end - start));

        if (updatePos) {
            startPos = start;
            startLine = line.lineNumber();
        }
    }
}

InlineMathParser::InlineMathParser() = default;

InlineMathParser::~InlineMathParser() = default;

bool InlineMathParser::check(Line &line,
                             ParagraphStream &stream,
                             InlineContext &ctx,
                             QSharedPointer<Document>,
                             const QString &,
                             const QString &,
                             QStringList &,
                             Parser &,
                             const ReverseSolidusHandler &rs)
{
    if (line.currentChar() == s_dollarSignChar && !rs.isPrevReverseSolidus()) {
        const auto startDelimPos = line.position();
        Line::State lState = line.currentState();
        int dollarCount = 0;
        auto startLine = line.lineNumber();

        skipIf(line, [&lState, &line, &dollarCount](const QChar &c) {
            if (c == s_dollarSignChar) {
                lState = line.currentState();
                ++dollarCount;

                return true;
            } else {
                return false;
            }
        });

        auto startPos = line.position();
        auto checkPos = isEmptyLine(line);
        auto first = true;

        if (dollarCount == 1) {
            if (line.position() < line.length() && line.currentChar().isSpace()) {
                line.restoreState(&lState);
                return false;
            }
        }

        if (dollarCount && dollarCount < 3) {
            const auto sState = stream.currentState();

            qsizetype endPos = -1;
            qsizetype endLine = line.lineNumber();

            QString code;

            auto tmp = line;

            while (true) {
                ReverseSolidusHandler rs;

                const auto pos = tmp.position();

                while (tmp.position() < tmp.length()) {
                    if (rs.isNotEscaped(tmp.currentChar())) {
                        if (tmp.currentChar() == s_dollarSignChar) {
                            const auto endDelimPos = tmp.position();

                            int closeDollarCount = 0;

                            skipIf(tmp, [&closeDollarCount](const QChar &c) {
                                if (c == s_dollarSignChar) {
                                    ++closeDollarCount;

                                    return true;
                                } else {
                                    return false;
                                }
                            });

                            if (closeDollarCount == dollarCount) {
                                bool validClosing = true;
                                if (dollarCount == 1) {
                                    if (endDelimPos > 0) {
                                        if (tmp.view().at(endDelimPos - 1).isSpace()) {
                                            validClosing = false;
                                        }
                                    } else {
                                        validClosing = false;
                                    }
                                    if (validClosing && tmp.position() < tmp.length()) {
                                        if (tmp.currentChar().isDigit()) {
                                            validClosing = false;
                                        }
                                    }
                                }

                                if (validClosing) {
                                    appendCode(tmp, pos, endDelimPos, code, checkPos && !first, startPos, startLine);

                                    auto item = QSharedPointer<Math>::create();
                                    item->setStartColumn(startPos);
                                item->setInline(dollarCount == 1);
                                item->setStartLine(startLine);
                                item->setEndColumn(endPos);
                                item->setEndLine(endDelimPos > 0 ? endLine : endLine - 1);
                                item->setStartDelim({startDelimPos,
                                                     line.lineNumber(),
                                                     startDelimPos + dollarCount - 1,
                                                     line.lineNumber()});
                                item->setEndDelim(
                                    {endDelimPos, tmp.lineNumber(), tmp.position() - 1, tmp.lineNumber()});

                                if (code.startsWith(s_graveAccentChar) && code.endsWith(s_graveAccentChar)) {
                                    code.removeFirst();
                                    code.removeLast();
                                }

                                item->setExpr(code);

                                ctx.inlines().append(item);

                                line = tmp;

                                return true;
                                }
                            }
                        }
                    }

                    endPos = tmp.position();

                    tmp.nextChar();
                    rs.next();
                }

                appendCode(tmp, pos, tmp.position(), code, checkPos && !first, startPos, startLine);

                if (stream.atEnd()) {
                    break;
                }

                tmp = stream.readLine();
                endLine = tmp.lineNumber();
                checkPos = checkPos && first;
                first = false;
            }

            stream.restoreStateBefore(sState);
            line = stream.readLine();
        }

        line.restoreState(&lState);
    }

    return false;
}

QString InlineMathParser::startDelimiterSymbols() const
{
    return QStringLiteral("$");
}

} /* namespace MD */
