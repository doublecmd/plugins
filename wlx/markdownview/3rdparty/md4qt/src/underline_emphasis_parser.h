/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_UNDERLINE_EMPHASIS_PARSER_H_INCLUDED
#define MD4QT_MD_UNDERLINE_EMPHASIS_PARSER_H_INCLUDED

// md4qt include.
#include "asterisk_emphasis_parser.h"

namespace MD
{

/*!
 * \class MD::UnderlineEmphasisParser
 * \inmodule md4qt
 * \inheaderfile md4qt/underline_emphasis_parser.h
 *
 * \brief Underline emphasis parser.
 *
 * Underline emphasis parser.
 */
class UnderlineEmphasisParser : public AsteriskEmphasisParser
{
public:
    UnderlineEmphasisParser();
    ~UnderlineEmphasisParser() override;

    /*!
     * Returns delimiter character for this kind of emphasis.
     */
    const QChar &symbol() const override;

    /*!
     * Returns whether the given sequence of delimiters is a left flanking.
     *
     * \a before Symbol before the sequence, or null character if it's a beginning of a line.
     *
     * \a after Symbol after the sequence, or null character if it's an end of a line.
     */
    bool isLeftFlanking(const QChar &before,
                        const QChar &after) const override;

    /*!
     * Returns whether the given sequence of delimiters is a right flanking.
     *
     * \a before Symbol before the sequence, or null character if it's a beginning of a line.
     *
     * \a after Symbol after the sequence, or null character if it's an end of a line.
     */
    bool isRightFlanking(const QChar &before,
                         const QChar &after) const override;

private:
    static const QChar m_symbol;
}; // class AsteriskEmphasisParser

} /* namespace MD */

#endif // MD4QT_MD_UNDERLINE_EMPHASIS_PARSER_H_INCLUDED
