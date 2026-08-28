/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "text_stream.h"

namespace MD
{

//
// Line
//

Line::Line()
    : m_lineNumber(-1)
{
}

Line::Line(QStringView v,
           qsizetype ln)
    : m_view(v)
    , m_lineNumber(ln)
{
}

Line::State::State()
{
}

Line::State::State(qsizetype column,
                   qsizetype pos)
    : m_column(column)
    , m_pos(pos)
{
}

Line::RollbackLine::RollbackLine(Line &line)
    : m_line(line)
    , m_state(m_line.currentState())
{
}

Line::RollbackLine::~RollbackLine()
{
    if (m_enabled) {
        m_line.restoreState(&m_state);
    }
}

void Line::RollbackLine::enable(bool on)
{
    m_enabled = on;
}

//
// TextStreamBase
//

TextStreamBase::~TextStreamBase() = default;

Line TextStreamBase::currentLine()
{
    if (m_hashedLines.contains(m_current.m_lineNumber - 1)) {
        auto line = m_hashedLines.value(m_current.m_lineNumber - 1);

        return Line(line.first, line.second.m_lineNumber - 1);
    } else {
        return {};
    }
}

Line TextStreamBase::readLine()
{
    if (m_hashedLines.contains(m_current.m_lineNumber)) {
        auto line = m_hashedLines.value(m_current.m_lineNumber);

        restoreState(&line.second);

        return Line(line.first, line.second.m_lineNumber - 1);
    }

    auto makeLine = [this](QStringView view, const State &state) -> auto {
        this->m_hashedLines.insert(state.m_lineNumber - 1, qMakePair(view, state));

        return Line(view, state.m_lineNumber - 1);
    };

    const auto start = m_current.m_pos;
    bool rFound = false;
    ++m_current.m_lineNumber;

    while (!atEnd()) {
        bool decrement = true;
        const auto c = getChar();

        if (atEnd() && (c == s_carriageReturnChar || c == s_newLineChar) && !m_current.m_isLastNewLine) {
            m_current.m_isLastNewLine = true;
            --m_current.m_pos;
            decrement = false;
        }

        if (rFound && c != s_newLineChar) {
            if (!m_current.m_isLastNewLine) {
                --m_current.m_pos;
            }

            return makeLine(QStringView(data() + start, m_current.m_pos - start - 1), m_current);
        }

        if (c == s_newLineChar) {
            return makeLine(
                QStringView(data() + start, m_current.m_pos - start - (decrement ? 1 : 0) - (rFound ? 1 : 0)),
                m_current);
        }

        rFound = (c == s_carriageReturnChar);
    }

    if (!isEmpty()) {
        return makeLine(QStringView(data() + start, m_current.m_pos - start - (rFound ? 1 : 0)), m_current);
    } else {
        return Line(QStringView(), -1);
    }
}

void TextStreamBase::saveState()
{
    m_saved = m_current;
}

void TextStreamBase::restoreState(const State *state)
{
    m_current = (state ? *state : m_saved);
}

TextStreamBase::State TextStreamBase::currentState() const
{
    return m_current;
}

Line TextStreamBase::moveTo(qsizetype ln)
{
    if (m_hashedLines.contains(ln)) {
        auto line = m_hashedLines.value(ln);

        restoreState(&line.second);

        return Line(line.first, line.second.m_lineNumber - 1);
    }

    return Line(QStringView(), -1);
}

//
// TextStream
//

TextStream::TextStream(QTextStream &stream)
    : m_data(stream.readAll())
{
}

bool TextStream::atEnd() const
{
    return (m_current.m_pos == m_data.length());
}

QChar TextStream::getChar()
{
    if (!atEnd()) {
        return m_data[m_current.m_pos++];
    } else {
        return {};
    }
}

const QChar *TextStream::data() const
{
    return m_data.data();
}

bool TextStream::isEmpty() const
{
    return m_data.isEmpty();
}

//
// ParagraphStream
//

ParagraphStream::ParagraphStream(const HashedLines &lines,
                                 qsizetype firstLineNumber,
                                 qsizetype lastLineNumber)
    : m_lines(lines)
    , m_lastLineNumber(lastLineNumber)
{
    State st;
    st.m_lineNumber = firstLineNumber;

    restoreState(&st);
}

Line ParagraphStream::readLine()
{
    return (!atEnd() ? m_lines.value(m_currentState.m_lineNumber++) : Line());
}

bool ParagraphStream::atEnd() const
{
    return (m_currentState.m_lineNumber > m_lastLineNumber);
}

void ParagraphStream::saveState()
{
    m_savedState = m_currentState;
}

void ParagraphStream::restoreState(const State *state)
{
    m_currentState = (state ? *state : m_savedState);
}

void ParagraphStream::restoreStateBefore(const State &st)
{
    m_currentState.m_lineNumber = st.m_lineNumber - 1;
}

ParagraphStream::State ParagraphStream::currentState() const
{
    return m_currentState;
}

bool ParagraphStream::State::operator==(const State &other) const
{
    return m_lineNumber == other.m_lineNumber;
}

bool ParagraphStream::State::operator!=(const State &other) const
{
    return !(operator==(other));
}

} /* namespace MD */
