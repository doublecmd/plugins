/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "strikethrough_emphasis_parser.h"

namespace MD
{

const QChar StrikethroughEmphasisParser::m_symbol = QLatin1Char('~');

StrikethroughEmphasisParser::StrikethroughEmphasisParser() = default;

StrikethroughEmphasisParser::~StrikethroughEmphasisParser() = default;

const QChar &StrikethroughEmphasisParser::symbol() const
{
    return m_symbol;
}

bool StrikethroughEmphasisParser::isEmphasis(int length) const
{
    return (length == 1 || length == 2);
}

bool StrikethroughEmphasisParser::isLengthCorrespond() const
{
    return true;
}

ItemWithOpts::Styles StrikethroughEmphasisParser::openStyles(qsizetype startPos,
                                                             qsizetype lineNumber,
                                                             qsizetype length) const
{
    ItemWithOpts::Styles styles;

    styles.append(StyleDelim(StrikethroughText, startPos, lineNumber, startPos + length - 1, lineNumber));

    return styles;
}

ItemWithOpts::Styles StrikethroughEmphasisParser::closeStyles(qsizetype startPos,
                                                              qsizetype lineNumber,
                                                              qsizetype length) const
{
    ItemWithOpts::Styles styles;

    styles.append(StyleDelim(StrikethroughText, startPos, lineNumber, startPos + length - 1, lineNumber));

    return styles;
}

} /* namespace MD */
