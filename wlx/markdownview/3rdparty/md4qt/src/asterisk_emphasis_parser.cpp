/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "asterisk_emphasis_parser.h"

namespace MD
{

const QChar AsteriskEmphasisParser::m_symbol = QLatin1Char('*');

AsteriskEmphasisParser::AsteriskEmphasisParser() = default;

AsteriskEmphasisParser::~AsteriskEmphasisParser() = default;

const QChar &AsteriskEmphasisParser::symbol() const
{
    return m_symbol;
}

bool AsteriskEmphasisParser::isEmphasis(int) const
{
    return true;
}

bool AsteriskEmphasisParser::isLengthCorrespond() const
{
    return false;
}

ItemWithOpts::Styles AsteriskEmphasisParser::openStyles(qsizetype startPos,
                                                        qsizetype lineNumber,
                                                        qsizetype length) const
{
    ItemWithOpts::Styles styles;

    if (length % 2 == 1) {
        styles.append(StyleDelim(ItalicText, startPos, lineNumber, startPos, lineNumber));
        ++startPos;
    }

    for (auto i = 0; i < length / 2; ++i) {
        styles.append(StyleDelim(BoldText, startPos, lineNumber, startPos + 1, lineNumber));
        startPos += 2;
    }

    return styles;
}

ItemWithOpts::Styles AsteriskEmphasisParser::closeStyles(qsizetype startPos,
                                                         qsizetype lineNumber,
                                                         qsizetype length) const
{
    ItemWithOpts::Styles styles;

    for (auto i = 0; i < length / 2; ++i) {
        styles.append(StyleDelim(BoldText, startPos, lineNumber, startPos + 1, lineNumber));
        startPos += 2;
    }

    if (length % 2 == 1) {
        styles.append(StyleDelim(ItalicText, startPos, lineNumber, startPos, lineNumber));
        ++startPos;
    }

    return styles;
}

} /* namespace MD */
