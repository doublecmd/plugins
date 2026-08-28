/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_STRIKETHROUGH_EMPHASIS_PARSER_H_INCLUDED
#define MD4QT_MD_STRIKETHROUGH_EMPHASIS_PARSER_H_INCLUDED

// md4qt include.
#include "emphasis_parser.h"

namespace MD
{

/*!
 * \class MD::StrikethroughEmphasisParser
 * \inmodule md4qt
 * \inheaderfile md4qt/strikethrough_emphasis_parser.h
 *
 * \brief Strikethrough emphasis parser.
 *
 * Strikethrough emphasis parser.
 */
class StrikethroughEmphasisParser : public EmphasisParser
{
public:
    StrikethroughEmphasisParser();
    ~StrikethroughEmphasisParser() override;

    /*!
     * Returns delimiter character for this kind of emphasis.
     */
    virtual const QChar &symbol() const override;

    /*!
     * Returns whether the sequence of the given length is an emphasis.
     *
     * \a length Length of the sequence.
     */
    virtual bool isEmphasis(int length) const override;

    /*!
     * Returns whether length of opening and closing delimiter should be equal to each other, or may differ.
     */
    virtual bool isLengthCorrespond() const override;

    /*!
     * Returns opening text option (style) for the given delimiter with the given length.
     *
     * \a startPos Start position of the first delimiter.
     *
     * \a lineNumber Line number.
     *
     * \a length Length of the delimiter.
     */
    virtual ItemWithOpts::Styles openStyles(qsizetype startPos,
                                            qsizetype lineNumber,
                                            qsizetype length) const override;

    /*!
     * Returns closing text option (style) for the given delimiter with the given length.
     *
     * \a startPos Start position of the first delimiter.
     *
     * \a lineNumber Line number.
     *
     * \a length Length of the delimiter.
     */
    virtual ItemWithOpts::Styles closeStyles(qsizetype startPos,
                                             qsizetype lineNumber,
                                             qsizetype length) const override;

private:
    static const QChar m_symbol;
}; // class StrikethroughEmphasisParser

} /* namespace MD */

#endif // MD4QT_MD_STRIKETHROUGH_EMPHASIS_PARSER_H_INCLUDED
