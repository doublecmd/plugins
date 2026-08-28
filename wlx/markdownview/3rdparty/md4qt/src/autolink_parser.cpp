/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "autolink_parser.h"
#include "constants.h"
#include "inline_context.h"
#include "parser.h"
#include "reverse_solidus.h"
#include "text_stream.h"
#include "utils.h"

namespace MD
{

QString readURI(Line &line)
{
    QString url;

    while (line.position() < line.length()) {
        url.append(line.currentChar());

        if (isAsciiControl(line.currentChar())) {
            return QString();
        } else if (line.currentChar() == s_greaterSignChar) {
            line.nextChar();

            url.removeLast();

            return url;
        } else if (line.currentChar() == s_lessSignChar) {
            return QString();
        }

        line.nextChar();
    }

    return QString();
}

AutolinkParser::AutolinkParser() = default;

AutolinkParser::~AutolinkParser() = default;

bool AutolinkParser::check(Line &line,
                           ParagraphStream &,
                           InlineContext &ctx,
                           QSharedPointer<Document>,
                           const QString &,
                           const QString &,
                           QStringList &,
                           Parser &parser,
                           const ReverseSolidusHandler &rs)
{
    if (line.currentChar() == s_lessSignChar && !rs.isPrevReverseSolidus()) {
        const auto lState = line.currentState();

        const auto startPos = line.position();
        const auto startLine = line.lineNumber();

        line.nextChar();

        const auto commonMark = (parser.autolinkUriValidation() == Parser::AutolinkUriValidation::CommonMark);
        const auto uri = readURI(line);
        const auto validUri = (commonMark ? isCommonMarkAutolinkUri(uri) : isValidUrl(uri));

        if (!validUri && !isEmail(uri)) {
            line.restoreState(&lState);

            return false;
        }

        auto lnk = QSharedPointer<Link>::create();
        lnk->setStartColumn(startPos);
        lnk->setStartLine(startLine);
        lnk->setEndColumn(line.position() - 1);
        lnk->setEndLine(line.lineNumber());
        lnk->setUrl(uri);
        lnk->setTextPos({startPos + 1, startLine, line.position() - 2, line.lineNumber()});
        lnk->setUrlPos(lnk->textPos());
        ctx.inlines().append(lnk);

        return true;
    }

    return false;
}

QString AutolinkParser::startDelimiterSymbols() const
{
    return QStringLiteral("<");
}

} /* namespace MD */
