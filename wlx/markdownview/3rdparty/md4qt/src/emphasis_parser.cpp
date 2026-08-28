/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "emphasis_parser.h"
#include "inline_context.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

// C++ include.
#include <algorithm>

namespace MD
{

EmphasisParser::EmphasisParser() = default;

EmphasisParser::~EmphasisParser() = default;

bool EmphasisParser::check(Line &currentLine,
                           ParagraphStream &,
                           InlineContext &ctx,
                           QSharedPointer<Document>,
                           const QString &,
                           const QString &,
                           QStringList &,
                           Parser &,
                           const ReverseSolidusHandler &rs)
{
    if (currentLine.currentChar() == symbol() && !rs.isPrevReverseSolidus()) {
        auto pos = currentLine.position();
        Line::State st;
        const auto prevSymbol = currentLine.prevChar();
        int count = 0;

        while (currentLine.currentChar() == symbol() && currentLine.position() < currentLine.length()) {
            ++count;
            st = currentLine.currentState();
            currentLine.nextChar();
        }

        const auto nextSymbol = currentLine.currentChar();

        if (isEmphasis(count)) {
            const auto leftFlanking = isLeftFlanking(prevSymbol, nextSymbol);
            const auto rightFlanking = isRightFlanking(prevSymbol, nextSymbol);

            if (leftFlanking || rightFlanking) {
                for (int i = 0; i < count; ++i) {
                    ctx.delims().enqueue({pos, currentLine.lineNumber(), leftFlanking, rightFlanking, this});

                    ++pos;
                }

                return true;
            } else {
                currentLine.restoreState(&st);
            }
        } else {
            currentLine.restoreState(&st);
        }
    }

    return false;
}

inline qsizetype delimLength(const StyleDelim &s)
{
    return s.endColumn() - s.startColumn() + 1;
}

bool EmphasisParser::isLeftFlanking(const QChar &before,
                                    const QChar &after) const
{
    const bool punctBefore = (before.isNull() ? true : before.isPunct() || before.isSymbol());
    const bool uWhitespaceBefore = (before.isNull() ? true : isUnicodeWhitespace(before));
    const bool uWhitespaceAfter = (after.isNull() ? true : isUnicodeWhitespace(after));
    const bool punctAfter = (after.isNull() ? true : after.isPunct() || after.isSymbol());
    const bool uWhitespaceOrPunctBefore = uWhitespaceBefore || punctBefore;

    return (!uWhitespaceAfter && (!punctAfter || (punctAfter && uWhitespaceOrPunctBefore)));
}

bool EmphasisParser::isRightFlanking(const QChar &before,
                                     const QChar &after) const
{
    const bool punctBefore = (before.isNull() ? true : before.isPunct() || before.isSymbol());
    const bool uWhitespaceBefore = (before.isNull() ? true : isUnicodeWhitespace(before));
    const bool uWhitespaceAfter = (after.isNull() ? true : isUnicodeWhitespace(after));
    const bool punctAfter = (after.isNull() ? true : after.isPunct() || after.isSymbol());

    return (!uWhitespaceBefore && (!punctBefore || (punctBefore && (uWhitespaceAfter || punctAfter))));
}

int lengthForward(const InlineContext::DelimiterQueue &emph,
                  qsizetype idx)
{
    const auto *parser = emph[idx].m_parser;
    int len = 1;
    const auto pos = emph[idx].m_pos;

    for (qsizetype i = idx + 1; i < emph.size(); ++i) {
        if (emph[i].m_line == emph[idx].m_line && emph[i].m_parser == parser && emph[i].m_pos == pos + len) {
            ++len;
        } else {
            break;
        }
    }

    return len;
}

int lengthBackward(const InlineContext::DelimiterQueue &emph,
                   qsizetype idx)
{
    const auto *parser = emph[idx].m_parser;
    int len = 1;
    const auto pos = emph[idx].m_pos;

    for (qsizetype i = idx - 1; i >= 0; --i) {
        if (emph[i].m_line == emph[idx].m_line && emph[i].m_parser == parser && emph[i].m_pos == pos - len) {
            ++len;
        } else {
            break;
        }
    }

    return len;
}

inline bool isMult3(long long int i1,
                    long long int i2)
{
    return ((((i1 + i2) % 3) == 0) && !((i1 % 3 == 0) && (i2 % 3 == 0)));
}

inline EmphasisParser *staticCast(InlineParser *parser)
{
    return static_cast<EmphasisParser *>(parser);
}

void EmphasisParser::processEmphasises(InlineContext &ctx)
{
    if (!ctx.delims().isEmpty()) {
        for (qsizetype i = 0; i < ctx.delims().size(); ++i) {
            if (ctx.delims()[i].m_rightFlanking) {
                const auto lengthRight = lengthForward(ctx.delims(), i);
                const auto tmpLengthLeft = lengthBackward(ctx.delims(), i) - 1;

                bool processed = false;

                for (qsizetype j = i - tmpLengthLeft - 1; j >= 0; --j) {
                    if (ctx.delims()[j].m_parser == ctx.delims()[i].m_parser && ctx.delims()[j].m_leftFlanking) {
                        const auto lengthLeft = lengthBackward(ctx.delims(), j);

                        bool canMatch = !(((ctx.delims()[i].m_leftFlanking && ctx.delims()[i].m_rightFlanking)
                                           || (ctx.delims()[j].m_leftFlanking && ctx.delims()[j].m_rightFlanking))
                                          && (isMult3(lengthLeft, lengthRight + tmpLengthLeft)));
                        canMatch = canMatch
                            && (staticCast(ctx.delims()[i].m_parser)->isLengthCorrespond() ? lengthLeft == lengthRight
                                                                                           : true);

                        if (canMatch) {
                            processed = true;

                            ctx.delims().remove(j + 1, i - j - 1);
                            i -= i - j - 1;

                            if (lengthLeft <= lengthRight) {
                                ctx.openStyles().append(staticCast(ctx.delims()[i - 1].m_parser)
                                                            ->openStyles(ctx.delims()[i - lengthLeft].m_pos,
                                                                         ctx.delims()[i - 1].m_line,
                                                                         lengthLeft));
                                ctx.closeStyles().append(
                                    staticCast(ctx.delims()[i].m_parser)
                                        ->closeStyles(ctx.delims()[i].m_pos, ctx.delims()[i].m_line, lengthLeft));
                                ctx.delims().remove(i, lengthLeft);
                                ctx.delims().remove(i - lengthLeft, lengthLeft);
                                i -= lengthLeft + 1;
                            } else {
                                ctx.openStyles().append(staticCast(ctx.delims()[i - 1].m_parser)
                                                            ->openStyles(ctx.delims()[i - lengthRight].m_pos,
                                                                         ctx.delims()[i - 1].m_line,
                                                                         lengthRight));
                                ctx.closeStyles().append(
                                    staticCast(ctx.delims()[i].m_parser)
                                        ->closeStyles(ctx.delims()[i].m_pos, ctx.delims()[i].m_line, lengthRight));
                                ctx.delims().remove(i, lengthRight);
                                ctx.delims().remove(i - lengthRight, lengthRight);
                                i -= lengthRight + 1;
                            }

                            break;
                        } else {
                            j -= lengthLeft - 1;
                        }
                    }
                }

                if (!processed && !ctx.delims()[i].m_leftFlanking) {
                    ctx.delims().remove(i--);
                }
            }
        }

        ctx.delims().clear();

        std::sort(ctx.openStyles().begin(), ctx.openStyles().end());
        std::sort(ctx.closeStyles().begin(), ctx.closeStyles().end());
    }
}

QString EmphasisParser::startDelimiterSymbols() const
{
    return QString(symbol());
}

} /* namespace MD */
