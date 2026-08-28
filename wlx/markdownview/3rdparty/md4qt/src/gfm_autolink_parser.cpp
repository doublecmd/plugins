/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "gfm_autolink_parser.h"
#include "constants.h"
#include "inline_context.h"
#include "parser.h"
#include "reverse_solidus.h"
#include "utils.h"

namespace MD
{

inline bool isPrevSymbolAllowed(const QChar &c)
{
    static const QString s_allowed = QStringLiteral("*_~( ");

    return c.isNull() || s_allowed.contains(c);
}

inline QPair<QString,
             Line::State>
readLink(Line &line,
         QSharedPointer<LinkImageParser> linkParser,
         ParagraphStream &stream,
         InlineContext &ctx,
         QSharedPointer<Document> doc,
         const QString &path,
         const QString &fileName,
         QStringList &linksToParse,
         Parser &parser)
{
    const auto startPos = line.position();
    auto st = line.currentState();

    ReverseSolidusHandler rs;

    while (line.position() < line.length()) {
        if (rs.isNotEscaped(line.currentChar())) {
            if (line.currentChar().isSpace() || line.currentChar() == s_lessSignChar) {
                break;
            } else if (line.currentChar() == s_rightSquareBracketChar) {
                if (linkParser->check(line, stream, ctx, doc, path, fileName, linksToParse, parser, rs)) {
                    return qMakePair(QString(), st);
                }
            }
        }

        st = line.currentState();
        rs.next();
        line.nextChar();
    }

    return qMakePair(line.slicedCopy(startPos, line.position() - startPos), st);
}

inline qsizetype skipAtEnd(const QString &url)
{
    static const QString s_punct = QStringLiteral("?!.,:*_~");

    auto pos = skipIfBackward(url.length() - 1, url, [](const QChar &c) {
        return s_punct.contains(c);
    });

    qsizetype right = 0;

    if (url[pos] == s_rightParenthesisChar) {
        for (qsizetype i = 0; i <= pos; ++i) {
            if (url[i] == s_leftParenthesisChar) {
                --right;
            } else if (url[i] == s_rightParenthesisChar) {
                ++right;
            }
        }
    }

    if (right > 0) {
        pos -= right;
    }

    return url.length() - pos - 1;
}

inline void makeLink(const Line::State &start,
                     const QString &url,
                     InlineContext &ctx,
                     qsizetype lineNumber,
                     qsizetype length,
                     Line::State &end,
                     Line &line,
                     qsizetype skip)
{
    auto link = QSharedPointer<Link>::create();

    link->setUrl(url);
    link->setUrlPos({start.m_pos, lineNumber, start.m_pos + length - 1, lineNumber});
    link->setTextPos(link->urlPos());
    link->setStartColumn(start.m_pos);
    link->setStartLine(lineNumber);
    link->setEndColumn(start.m_pos + length - 1);
    link->setEndLine(lineNumber);

    ctx.inlines().append(link);

    end.m_pos -= skip;
    end.m_column -= skip;

    line.restoreState(&end);
}

inline bool isValidHttpAutolink(const QString &url,
                                Parser &parser)
{
    if (parser.autolinkUriValidation() == Parser::AutolinkUriValidation::CommonMark) {
        return isCommonMarkAutolinkUri(url);
    } else {
        QUrl u(url, QUrl::StrictMode);

        return (u.isValid() && !u.host().isEmpty());
    }
}

GfmAutolinkParser::GfmAutolinkParser(QSharedPointer<LinkImageParser> linkParser)
    : m_linkParser(linkParser)
{
}

GfmAutolinkParser::~GfmAutolinkParser() = default;

bool GfmAutolinkParser::check(Line &line,
                              ParagraphStream &stream,
                              InlineContext &ctx,
                              QSharedPointer<Document> doc,
                              const QString &path,
                              const QString &fileName,
                              QStringList &linksToParse,
                              Parser &parser,
                              const ReverseSolidusHandler &)
{
    if (isPrevSymbolAllowed(line.prevChar())) {
        const auto st = line.currentState();

        auto url = readLink(line, m_linkParser, stream, ctx, doc, path, fileName, linksToParse, parser);

        if (!url.first.isEmpty()) {
            const auto skip = skipAtEnd(url.first);

            if (url.first.startsWith(s_wwwString)) {
                url.first.prepend(s_httpString);
                url.first = url.first.left(url.first.length() - skip);

                if (isValidHttpAutolink(url.first, parser)) {
                    makeLink(st,
                             url.first,
                             ctx,
                             line.lineNumber(),
                             url.first.length() - s_httpString.length(),
                             url.second,
                             line,
                             skip);

                    return true;
                }
            } else if (url.first.startsWith(s_httpString)) {
                url.first = url.first.left(url.first.length() - skip);

                if (isValidHttpAutolink(url.first, parser)) {
                    makeLink(st, url.first, ctx, line.lineNumber(), url.first.length(), url.second, line, skip);

                    return true;
                }
            } else if (url.first.startsWith(s_httpsString)) {
                url.first = url.first.left(url.first.length() - skip);

                if (isValidHttpAutolink(url.first, parser)) {
                    makeLink(st, url.first, ctx, line.lineNumber(), url.first.length(), url.second, line, skip);

                    return true;
                }
            } else if (url.first.startsWith(s_mailtoString)) {
                url.first = url.first.left(url.first.length() - skip);

                auto email = url.first.sliced(s_mailtoString.length());

                if (isEmail(email)) {
                    makeLink(st, url.first, ctx, line.lineNumber(), url.first.length(), url.second, line, skip);

                    return true;
                }
            } else if (url.first.startsWith(s_xmppString)) {
            } else {
                url.first = url.first.left(url.first.length() - skip);

                if (isEmail(url.first)) {
                    makeLink(st,
                             s_mailtoString + url.first,
                             ctx,
                             line.lineNumber(),
                             url.first.length(),
                             url.second,
                             line,
                             skip);

                    return true;
                }
            }
        } else {
            return true;
        }

        line.restoreState(&st);
    }

    return false;
}

QString GfmAutolinkParser::startDelimiterSymbols() const
{
    return QStringLiteral("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

} /* namespace MD */
