/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_REVERSE_SOLIDUS_H_INCLUDED
#define MD4QT_MD_REVERSE_SOLIDUS_H_INCLUDED

// Qt include.
#include <QChar>

namespace MD
{

/*!
 * \class MD::ReverseSolidusHandler
 * \inmodule md4qt
 * \inheaderfile md4qt/reverse_solidus.h
 *
 * \brief Helper for process reverse solidus characters.
 *
 * Simplifier for algorithms that handles reverse solidus characters.
 */
class ReverseSolidusHandler
{
public:
    /*!
     * Process current character.
     *
     * Returns true if current character was recognized as reverse solidus, false otherwise.
     *
     * \a ch Current character.
     *
     * \a additionalRule Additional boolean value that should be true to recognize current character
     *                   as reverse solidus.
     */
    bool process(const QChar &ch,
                 bool additionalRule = true);

    /*!
     * Return whether previous character was reverse solidus.
     */
    bool isPrevReverseSolidus() const;

    /*!
     * Combination of processing of current character and checking that previous \b {WAS NOT}
     * a reverse solidus character.
     *
     * Returns true if current character \b {WAS NOT} recognized as reverse solidus and previous character
     * \b {WAS NOT} a reverse solidus (i.e not escaped and not reverse solidus), false otherwise.
     *
     * \a ch Current character.
     *
     * \a additionalRule Additional boolean value that should be true to recognize current character
     *                   as reverse solidus.
     */
    bool isNotEscaped(const QChar &ch,
                      bool additionalRule = true);

    /*!
     * Combination of processing of current character and checking that previous was
     * a reverse solidus character.
     *
     * Returns true if current character \b {WAS NOT} recognized as reverse solidus and previous character
     * \b {WAS} a reverse solidus (i.e escaped and not reverse solidus), false otherwise.
     *
     * \a ch Current character.
     *
     * \a additionalRule Additional boolean value that should be true to recognize current character
     *                   as reverse solidus.
     */
    bool isEscaped(const QChar &ch,
                   bool additionalRule = true);

    /*!
     * Should be invoked after processing of current character and all logic with current character.
     */
    void next();

    /*!
     * Clear state.
     */
    void clear();

private:
    bool m_now = false;
    bool m_reverseSolidus = false;
}; // struct ReverseSolidusHandler

} /* namespace MD */

#endif // MD4QT_MD_REVERSE_SOLIDUS_H_INCLUDED
