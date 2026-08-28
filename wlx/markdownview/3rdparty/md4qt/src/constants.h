/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_CONSTANTS_H_INCLUDED
#define MD4QT_MD_CONSTANTS_H_INCLUDED

// Qt include.
#include <QString>

namespace MD
{

static const auto s_reverseSolidusChar = QLatin1Char('\\');
static const auto s_solidusChar = QLatin1Char('/');
static const auto s_numberSignChar = QLatin1Char('#');
static const auto s_newLineChar = QLatin1Char('\n');
static const auto s_carriageReturnChar = QLatin1Char('\r');
static const auto s_tabChar = QLatin1Char('\t');
static const auto s_spaceChar = QLatin1Char(' ');
static const auto s_lessSignChar = QLatin1Char('<');
static const auto s_greaterSignChar = QLatin1Char('>');
static const auto s_leftSquareBracketChar = QLatin1Char('[');
static const auto s_rightSquareBracketChar = QLatin1Char(']');
static const auto s_colonChar = QLatin1Char(':');
static const auto s_leftParenthesisChar = QLatin1Char('(');
static const auto s_rightParenthesisChar = QLatin1Char(')');
static const auto s_ampersandChar = QLatin1Char('&');
static const auto s_semicolonChar = QLatin1Char(';');
static const auto s_xChar = QLatin1Char('x');
static const auto s_replaceChar = QChar(0xFFFD);
static const auto s_quotationMarkChar = QLatin1Char('"');
static const auto s_apostropheChar = QLatin1Char('\'');
static const auto s_asteriskChar = QLatin1Char('*');
static const auto s_minusChar = QLatin1Char('-');
static const auto s_lowLineChar = QLatin1Char('_');
static const auto s_rightCurlyBracketChar = QLatin1Char('}');
static const auto s_tildeChar = QLatin1Char('~');
static const auto s_graveAccentChar = QLatin1Char('`');
static const auto s_equalSignChar = QLatin1Char('=');
static const auto s_plusSignChar = QLatin1Char('+');
static const auto s_dotChar = QLatin1Char('.');
static const auto s_questionMarkChar = QLatin1Char('?');
static const auto s_exclamationMarkChar = QLatin1Char('!');
static const auto s_commercialAtChar = QLatin1Char('@');
static const auto s_circumflexAccentifChar = QLatin1Char('^');
static const auto s_verticalLineChar = QLatin1Char('|');
static const auto s_hChar = QLatin1Char('h');
static const auto s_wChar = QLatin1Char('w');
static const auto s_mChar = QLatin1Char('m');
static const auto s_dollarSignChar = QLatin1Char('$');

static const int s_tabSize = 4;

static const QString s_canBeEscaped = QStringLiteral("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");
static const QString s_startIdString = QStringLiteral("{#");
static const QString s_mathString = QStringLiteral("math");
static const QString s_commentStartString = QStringLiteral("!--");
static const QString s_cdataStartString = QStringLiteral("![cdata[");
static const QString s_cdataEndString = QStringLiteral("]]>");
static const QString s_mailtoString = QStringLiteral("mailto:");
static const QString s_xmppString = QStringLiteral("xmpp:");
static const QString s_wwwString = QStringLiteral("www.");
static const QString s_httpString = QStringLiteral("http://");
static const QString s_httpsString = QStringLiteral("https://");
static const QString s_commentEndString = QStringLiteral("-->");
static const QString s_processInstructionEndString = QStringLiteral("?>");

} /* namespace MD */

#endif // MD4QT_MD_CONSTANTS_H_INCLUDED
