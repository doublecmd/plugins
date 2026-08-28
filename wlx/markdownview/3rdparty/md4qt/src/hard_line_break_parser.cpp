/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "hard_line_break_parser.h"
#include "constants.h"
#include "inline_context.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

namespace MD
{

HardLineBreakParser::HardLineBreakParser() = default;

HardLineBreakParser::~HardLineBreakParser() = default;

bool HardLineBreakParser::check(Line &line,
                                ParagraphStream &stream,
                                InlineContext &ctx,
                                QSharedPointer<Document>,
                                const QString &,
                                const QString &,
                                QStringList &,
                                Parser &,
                                const ReverseSolidusHandler &rs)
{
    line.saveState();
    stream.saveState();

    if (!stream.atEnd() && !rs.isPrevReverseSolidus()) {
        if (line.currentChar() == s_spaceChar && line.position() < line.length() - 1) {
            const auto startPos = line.position();

            if (isEmptyLine(line)) {
                auto br = QSharedPointer<LineBreak>::create();
                br->setStartColumn(startPos);
                br->setStartLine(line.lineNumber());
                br->setEndColumn(line.length() - 1);
                br->setEndLine(line.lineNumber());

                ctx.inlines().append(br);

                line.skip();

                return true;
            }
        } else if (line.currentChar() == s_reverseSolidusChar && line.position() == line.length() - 1) {
            auto br = QSharedPointer<LineBreak>::create();
            br->setStartColumn(line.position());
            br->setStartLine(line.lineNumber());
            br->setEndColumn(line.position());
            br->setEndLine(line.lineNumber());

            line.skip();

            ctx.inlines().append(br);

            return true;
        }
    }

    line.restoreState();
    stream.restoreState();

    return false;
}

QString HardLineBreakParser::startDelimiterSymbols() const
{
    return QStringLiteral(" \\");
}

} /* namespace MD */
