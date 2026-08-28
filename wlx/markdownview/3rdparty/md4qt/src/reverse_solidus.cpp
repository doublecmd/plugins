/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "reverse_solidus.h"
#include "constants.h"

namespace MD
{

bool ReverseSolidusHandler::process(const QChar &ch,
                                    bool additionalRule)
{
    if (ch == s_reverseSolidusChar && !isPrevReverseSolidus() && additionalRule) {
        m_reverseSolidus = true;
        m_now = true;

        return true;
    } else {
        return false;
    }
}

bool ReverseSolidusHandler::isPrevReverseSolidus() const
{
    return m_reverseSolidus && !m_now;
}

bool ReverseSolidusHandler::isNotEscaped(const QChar &ch,
                                         bool additionalRule)
{
    return (!process(ch, additionalRule) && !isPrevReverseSolidus());
}

bool ReverseSolidusHandler::isEscaped(const QChar &ch,
                                      bool additionalRule)
{
    return (!process(ch, additionalRule) && isPrevReverseSolidus());
}

void ReverseSolidusHandler::next()
{
    if (!m_now) {
        m_reverseSolidus = false;
    }

    m_now = false;
}

void ReverseSolidusHandler::clear()
{
    m_now = false;
    m_reverseSolidus = false;
}

} /* namespace MD */
