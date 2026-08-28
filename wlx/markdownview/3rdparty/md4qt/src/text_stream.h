/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_TEXT_STREAM_H_INCLUDED
#define MD4QT_MD_TEXT_STREAM_H_INCLUDED

// md4qt include.
#include "constants.h"

// Qt include.
#include <QHash>
#include <QString>
#include <QStringView>
#include <QTextStream>

namespace MD
{

/*!
 * \class MD::Line
 * \inmodule md4qt
 * \inheaderfile md4qt/text_stream.h
 *
 * \brief Text line in the Markdown input document.
 *
 * Auxiliary string view like class to handle Markdown stuff. It handles tabulations and null
 * characters transparently for a developer. Current column in this class takes into account
 * a tabulation as needed.
 */
class Line
{
public:
    Line();
    /*!
     * Constructor.
     *
     * \a v String view.
     *
     * \ ln Line number.
     */
    Line(QStringView v,
         qsizetype ln);

    /*!
     * Returns column of the current character (taking into account variable length of tabulations).
     */
    inline qsizetype column() const
    {
        return m_currentState.m_column;
    }

    /*!
     * Returns position of the current charcter in the string (where tabulation is one character).
     */
    inline qsizetype position() const
    {
        return m_currentState.m_pos;
    }

    /*!
     * Move current character to the end of string.
     */
    void skip()
    {
        m_currentState.m_pos = length();
    }

    /*!
     * Returns length of the string.
     */
    inline qsizetype length() const
    {
        return m_view.length();
    }

    /*!
     * Returns line number.
     */
    inline qsizetype lineNumber() const
    {
        return m_lineNumber;
    }

    /*!
     * Returns previous character.
     */
    inline QChar prevChar() const
    {
        return (m_currentState.m_pos > 0 ? convert(m_view[m_currentState.m_pos - 1]) : QChar());
    }

    /*!
     * Return current character.
     */
    inline QChar currentChar() const
    {
        return (m_currentState.m_pos < length() ? convert(m_view[m_currentState.m_pos]) : QChar());
    }

    /*!
     * Returns next character.
     */
    inline QChar nextChar()
    {
        if (m_currentState.m_pos + 1 < length()) {
            if (m_view[m_currentState.m_pos] == s_tabChar) {
                m_currentState.m_column += s_tabSize - m_currentState.m_column % s_tabSize;
            } else {
                ++m_currentState.m_column;
            }

            return convert(m_view[++m_currentState.m_pos]);
        } else {
            if (m_currentState.m_pos < length()) {
                m_currentState.m_pos = length();
                ++m_currentState.m_column;
            }

            return {};
        }
    }

    /*!
     * Returns whether this string is empty.
     */
    inline bool isEmpty() const
    {
        return m_view.isEmpty();
    }

    /*!
     * \class MD::Line::State
     * \inmodule md4qt
     * \inheaderfile md4qt/text_stream.h
     *
     * \brief Current state of the line.
     *
     * State of the line.
     */
    struct State {
        State();
        State(qsizetype column,
              qsizetype pos);

        /*!
         * Column.
         */
        qsizetype m_column = 0;
        /*!
         * Position.
         */
        qsizetype m_pos = 0;
    }; // struct State

    /*!
     * Save state.
     */
    inline void saveState()
    {
        m_savedState = m_currentState;
    }

    /*!
     * Restore state.
     *
     * \a state State.
     */
    inline void restoreState(const State *state = nullptr)
    {
        m_currentState = (state ? *state : m_savedState);
    }

    /*!
     * Returns current state.
     */
    inline State currentState() const
    {
        return m_currentState;
    }

    /*!
     * Returns view.
     */
    inline const QStringView &view() const
    {
        return m_view;
    }

    /*!
     * Returns whether line at end.
     */
    inline bool atEnd() const
    {
        return (position() == length());
    }

    /*!
     * \class MD::Line::RollbackLine
     * \inmodule md4qt
     * \inheaderfile md4qt/text_stream.h
     *
     * \brief Struct to make a rollback of line state on destruction.
     *
     * Auxiliary struct to make a rollback of line state on destruction. On construction state will be saved.
     */
    struct RollbackLine {
        explicit RollbackLine(Line &line);
        ~RollbackLine();

        void enable(bool on = true);

    private:
        Line &m_line;
        State m_state;
        bool m_enabled = true;
    }; // struct RollbackLine

    /*!
     * Make a rollback point.
     */
    inline RollbackLine makeRollbackObject()
    {
        return RollbackLine(*this);
    }

    /*!
     * Returns sliced copy of string.
     *
     * \a pos Start position
     *
     * \a len Length.
     */
    inline QString slicedCopy(qsizetype pos,
                              qsizetype len = -1) const
    {
        if (len == -1) {
            len = length() - pos;
        }

        return QString(view().sliced(pos, len)).replace(QChar(), s_replaceChar);
    }

    /*!
     * Returns sliced line.
     *
     * \a pos Start position
     *
     * \a len Length.
     */
    inline Line sliced(qsizetype pos,
                       qsizetype len = -1)
    {
        if (len == -1) {
            len = length() - pos;
        }

        return Line(view().sliced(pos, len), m_lineNumber);
    }

private:
    inline QChar convert(const QChar &c) const
    {
        if (c.isNull()) {
            return s_replaceChar;
        } else if (c == s_tabChar) {
            return s_spaceChar;
        } else {
            return c;
        }
    }

private:
    QStringView m_view;
    qsizetype m_lineNumber = 0;
    State m_currentState;
    State m_savedState;
}; // class Line

/*!
 * \class MD::TextStreamBase
 * \inmodule md4qt
 * \inheaderfile md4qt/text_stream.h
 *
 * \brief Base class for text stream.
 *
 * This is common things for text streams used for splitting string into lines.
 */
class TextStreamBase
{
protected:
    virtual ~TextStreamBase();

public:
    /*!
     * Returns whether stream at end.
     */
    virtual bool atEnd() const = 0;

    /*!
     * Returns current line from stream and moves to next line.
     */
    Line readLine();

    /*!
     * Returns current line from stream without any modifications of state.
     */
    Line currentLine();

    /*!
     * \class MD::TextStreamBase::State
     * \inmodule md4qt
     * \inheaderfile md4qt/text_stream.h
     *
     * \brief Current state of the stream.
     *
     * State of the stream.
     */
    struct State {
        /*!
         * Current position in stream.
         */
        qsizetype m_pos = 0;
        /*!
         * Current line number.
         */
        qsizetype m_lineNumber = 0;
        /*!
         * Flags about last new line.
         */
        bool m_isLastNewLine = false;
    }; // struct state

    /*!
     * Save state (position) of stream to be able to restore it.
     */
    void saveState();

    /*!
     * Restore state.
     *
     * \a state State.
     */
    void restoreState(const State *state = nullptr);

    /*!
     * Returns current state.
     */
    State currentState() const;

    /*!
     * Moves stream position to the beginning of the given line number.
     *
     * Returns line with the given line number.
     *
     * \note This method does not take effect on not already read line.
     *
     * \a ln Line number.
     */
    Line moveTo(qsizetype ln);

protected:
    /*!
     * Returns current character from stream and moves to next character.
     */
    virtual QChar getChar() = 0;

    /*!
     * Returns data.
     */
    virtual const QChar *data() const = 0;

    /*!
     * Returns whether this stream is empty.
     */
    virtual bool isEmpty() const = 0;

protected:
    /*!
     * Current state of the stream.
     */
    State m_current;
    /*!
     * Saved state.
     */
    State m_saved;

private:
    QHash<qsizetype, QPair<QStringView, State>> m_hashedLines;
}; // class TextStreamBase

/*!
 * \class MD::TextStream
 * \inmodule md4qt
 * \inheaderfile md4qt/text_stream.h
 *
 * \brief Actual text stream.
 *
 * Text stream which task is to split string into lines. It handles "\n", "\r", "\r\n" correctly.
 */
class TextStream final : public TextStreamBase
{
public:
    explicit TextStream(QTextStream &stream);

    bool atEnd() const override;

protected:
    QChar getChar() override;
    const QChar *data() const override;
    bool isEmpty() const override;

private:
    QString m_data;
}; // class TextStream

/*!
 * \class MD::ParagraphStream
 * \inmodule md4qt
 * \inheaderfile md4qt/text_stream.h
 *
 * \brief Text stream for paragraph processing.
 *
 * Text stream for paragraph processing.
 */
class ParagraphStream final
{
public:
    /*!
     * \inmodule md4qt
     * \typealias MD::ParagraphStream::HashedLines
     * \inheaderfile md4qt/text_stream.h
     *
     * \brief Type of hashed lines.
     */
    using HashedLines = QHash<qsizetype, Line>;

    ParagraphStream(const HashedLines &lines,
                    qsizetype firstLineNumber,
                    qsizetype lastLineNumber);

    /*!
     * Returns next line.
     */
    Line readLine();

    /*!
     * Returns whether the stream at end.
     */
    bool atEnd() const;

    /*!
     * \class MD::ParagraphStream::State
     * \inmodule md4qt
     * \inheaderfile md4qt/text_stream.h
     *
     * \brief Current state of the stream.
     *
     * State of the stream.
     */
    struct State {
        /*!
         * Current line number.
         */
        qsizetype m_lineNumber = 0;

        bool operator==(const State &other) const;
        bool operator!=(const State &other) const;
    }; // struct state

    /*!
     * Save state (position) of stream to be able to restore it.
     */
    void saveState();

    /*!
     * Restore state.
     *
     * \a state State.
     */
    void restoreState(const State *state = nullptr);

    /*!
     * Restore state before the given.
     *
     * \a st State.
     */
    void restoreStateBefore(const State &st);

    /*!
     * Returns current state.
     */
    State currentState() const;

private:
    HashedLines m_lines;
    State m_currentState;
    State m_savedState;
    qsizetype m_lastLineNumber;
}; // class ParagraphStream

} /* namespace MD */

#endif // MD4QT_MD_TEXT_STREAM_H_INCLUDED
