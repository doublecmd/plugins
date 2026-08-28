/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "underline_emphasis_parser.h"

namespace MD
{

const QChar UnderlineEmphasisParser::m_symbol = QLatin1Char('_');

UnderlineEmphasisParser::UnderlineEmphasisParser() = default;

UnderlineEmphasisParser::~UnderlineEmphasisParser() = default;

const QChar &UnderlineEmphasisParser::symbol() const
{
    return m_symbol;
}

bool UnderlineEmphasisParser::isLeftFlanking(const QChar &before,
                                             const QChar &after) const
{
    return AsteriskEmphasisParser::isLeftFlanking(before, after)
        && !(before.isLetterOrNumber() && after.isLetterOrNumber());
}

bool UnderlineEmphasisParser::isRightFlanking(const QChar &before,
                                              const QChar &after) const
{
    return AsteriskEmphasisParser::isRightFlanking(before, after)
        && !(before.isLetterOrNumber() && after.isLetterOrNumber());
}

} /* namespace MD */
