/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_INLINE_CONTEXT_H_INCLUDED
#define MD4QT_MD_INLINE_CONTEXT_H_INCLUDED

// md4qt include.
#include "doc.h"

// Qt include.
#include <QQueue>

namespace MD
{

class InlineParser;

/*!
 * \class MD::InlineContext
 * \inmodule md4qt
 * \inheaderfile md4qt/inline_context.h
 *
 * \brief Inline parsing context.
 *
 * Auxiliary class for inlines parsing process.
 */
class InlineContext final
{
public:
    InlineContext();
    ~InlineContext();

    /*!
     * \class MD::InlineContext::Delimiter
     * \inmodule md4qt
     * \inheaderfile md4qt/inline_context.h
     *
     * \brief Delimiter description.
     *
     * Delimiter description.
     */
    struct Delimiter {
        /*!
         * Position.
         */
        qsizetype m_pos = -1;
        /*!
         * Line number.
         */
        qsizetype m_line = -1;
        /*!
         * Is left flanking?
         *
         * \note Used in emphasises parsing only.
         */
        bool m_leftFlanking = false;
        /*!
         * Is right flanking?
         *
         * \note Used in emphasises parsing only.
         */
        bool m_rightFlanking = false;
        /*!
         * Parser that produced this delimiter.
         */
        InlineParser *m_parser = nullptr;
    }; // struct Emphasis

    /*!
     * \typealias MD::InlineContext::DelimiterQueue
     * \inmodule md4qt
     * \inheaderfile md4qt/inline_context.h
     *
     * \brief Type of queue of delimiters.
     */
    using DelimiterQueue = QQueue<Delimiter>;

    /*!
     * Returns delimiters queue.
     */
    inline DelimiterQueue &delims()
    {
        return m_delims;
    }

    /*!
     * Returns parsed all opening styles.
     */
    inline ItemWithOpts::Styles &openStyles()
    {
        return m_openStyles;
    }

    /*!
     * Returns parsed all closing styles.
     */
    inline ItemWithOpts::Styles &closeStyles()
    {
        return m_closeStyles;
    }

    /*!
     * \typealias MD::InlineContext::InlinesList
     * \inmodule md4qt
     * \inheaderfile md4qt/inline_context.h
     *
     * \brief Type of list of parsed inlines.
     */
    using InlinesList = QVector<QSharedPointer<ItemWithOpts>>;

    /*!
     * Returns list of parsed inlines.
     */
    inline InlinesList &inlines()
    {
        return m_inlines;
    }

private:
    DelimiterQueue m_delims;
    InlinesList m_inlines;
    ItemWithOpts::Styles m_openStyles;
    ItemWithOpts::Styles m_closeStyles;
}; // class InlineContext

} /* namespace MD */

#endif // MD4QT_MD_INLINE_CONTEXT_H_INCLUDED
