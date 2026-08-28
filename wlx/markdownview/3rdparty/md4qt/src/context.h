/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_CONTEXT_H_INCLUDED
#define MD4QT_MD_CONTEXT_H_INCLUDED

// md4qt include.
#include "doc.h"
#include "text_stream.h"
#include "utils.h"

// Qt include.
#include <QHash>
#include <QQueue>
#include <QSet>

// C++ include.
#include <algorithm>

namespace MD
{

class BlockParser;

/*!
 * \class MD::Context
 * \inmodule md4qt
 * \inheaderfile md4qt/context.h
 *
 * \brief Parsing context.
 *
 * Auxiliary class for parsing process.
 */
class Context final
{
public:
    explicit Context(Context *prnt = nullptr);
    ~Context();

    /*!
     * Returns parent contex.
     */
    inline Context *parent() const
    {
        return m_parent;
    }

    /*!
     * \typealias MD::Context::Children
     * \inmodule md4qt
     * \inheaderfile md4qt/context.h
     *
     * \brief Type of queue of children.
     */
    using Children = QQueue<Context>;

    /*!
     * \typealias MD::Context::ChildIndents
     * \inmodule md4qt
     * \inheaderfile md4qt/context.h
     *
     * \brief Type of list of child indents.
     */
    using ChildIndents = QVector<QPair<qsizetype, QChar>>;

    /*!
     * \typealias MD::Context::ChildLists
     * \inmodule md4qt
     * \inheaderfile md4qt/context.h
     *
     * \brief Type of list of child lists.
     */
    using ChildLists = QVector<QSharedPointer<List>>;

    /*!
     * \typealias MD::Context::LineInfo
     * \inmodule md4qt
     * \inheaderfile md4qt/context.h
     *
     * \brief Type hash of line informations.
     */
    using LineInfo = QHash<qsizetype, Line::State>;

    /*!
     * Returns block parser of this context.
     */
    inline BlockParser *block() const
    {
        return m_block;
    }

    /*!
     * Set block parser.
     *
     * \a b Pointer to block.
     */
    inline void setBlock(BlockParser *b)
    {
        m_block = b;
    }

    /*!
     * Returns child contexts.
     */
    inline Children &children()
    {
        return m_children;
    }

    /*!
     * Returns child contexts.
     */
    inline const Children &children() const
    {
        return m_children;
    }

    /*!
     * Returns current indent column. Has a sense in lists, for example.
     */
    inline qsizetype indentColumn() const
    {
        return (m_dontConsiderIndents ? 0 : m_indent);
    }

    /*!
     * Set current indent column.
     *
     * \a i Indent.
     */
    inline void setIndentColumn(qsizetype i)
    {
        m_indent = i;
    }

    /*!
     * Returns whether any child indent is set.
     */
    inline bool hasChildIndents() const
    {
        return !m_childIndents.isEmpty();
    }

    /*!
     * Returns child indents.
     */
    inline ChildIndents &childIndents()
    {
        return m_childIndents;
    }

    /*!
     * Returns child indents.
     */
    inline const ChildIndents &childIndents() const
    {
        return m_childIndents;
    }

    /*!
     * Returns indent column for the first children.
     */
    inline qsizetype firstChildIndent() const
    {
        return (hasChildIndents() ? m_childIndents.front().first + indentFromColumn(indentColumn()) : indentColumn());
    }

    /*!
     * Returns indent column for the last children.
     */
    inline qsizetype lastChildIndent() const
    {
        return (hasChildIndents() ? m_childIndents.back().first + indentFromColumn(indentColumn()) : indentColumn());
    }

    /*!
     * Returns max available indent, checking in children and all parents.
     */
    inline qsizetype maxAvailableIndent() const
    {
        return std::max(lastChildIndent(), (parent() ? parent()->lastChildIndent() : 0));
    }

    /*!
     * Returns indents nesting level.
     */
    inline qsizetype indentsNestingLevel() const
    {
        return m_childIndents.size();
    }

    /*!
     * Append indentColumn to child indents.
     *
     * \a i Indent.
     *
     * \a d Delimiter.
     */
    inline void appendChildIndent(qsizetype i,
                                  const QChar &d = QChar())
    {
        const auto ci = i - (i ? 1 : 0) - indentFromColumn(indentColumn());

        m_childIndents.append(qMakePair(ci, d));

        updateParentContextForAllChildren();
    }

    /*!
     * Remove all indents greater then given.
     *
     * \a sc Count of spaces before.
     *
     * \a removeAdditional Means that list with first deletable indent should be deleted too.
     */
    inline void clearChildIndents(qsizetype sc,
                                  bool removeAdditional)
    {
        qsizetype count = 0;
        m_childIndents.removeIf([&sc, &count, this](const auto &v) {
            const auto will = v.first + indentFromColumn(this->indentColumn()) >= sc;

            if (will) {
                ++count;
            }

            return will;
        });

        if (!removeAdditional) {
            --count;
        }

        if (count > 0 && !m_lists.isEmpty()) {
            m_lists.remove(m_lists.size() - count, count);
        }
    }

    /*!
     * Returns lists.
     */
    ChildLists &lists()
    {
        return m_lists;
    }

    /*!
     * Returns lists.
     */
    const ChildLists &lists() const
    {
        return m_lists;
    }

    /*!
     * Returns whether most nested front child is lazy.
     *
     * \a lineNumber Line number.
     */
    inline bool isLazyLine(qsizetype lineNumber) const
    {
        return (m_lazy.contains(lineNumber) ? true : (m_parent ? m_parent->isLazyLine(lineNumber) : false));
    }

    /*!
     * Mark line with the given line number as lazy.
     *
     * \a lineNumber Line number.
     */
    inline void appendLazyInfo(qsizetype lineNumber)
    {
        m_lazy.insert(lineNumber);
    }

    /*!
     * Apply settings for child context.
     *
     * \a ctx Context.
     */
    void applyParentContext(Context &ctx);

    /*!
     * Update parent context for all children.
     */
    void updateParentContextForAllChildren();

    /*!
     * Return information about all lines.
     */
    inline const LineInfo &lineInfo() const
    {
        return m_lines;
    }

    /*!
     * Merge information about lines.
     *
     * \a other Line info.
     */
    inline void mergeLineInfo(const LineInfo &other)
    {
        m_lines.insert(other);
    }

    /*!
     * Add information about line of this block.
     *
     * \a lineNumber Line number.
     *
     * \a state State.
     */
    inline void appendLineInfo(qsizetype lineNumber,
                               const Line::State &state)
    {
        m_lines.insert(lineNumber, state);
    }

    /*!
     * Returns start position of this block on line.
     *
     * \a lineNumber Line number.
     */
    inline Line::State startPos(qsizetype lineNumber) const
    {
        return m_lines.value(lineNumber, Line::State(-1, -1));
    }

    /*!
     * Update start position of the line.
     *
     * \a lineNumber Line number.
     *
     * \a pos Position.
     */
    inline void updateStartPos(qsizetype lineNumber,
                               qsizetype pos)
    {
        if (m_lines.contains(lineNumber)) {
            m_lines[lineNumber].m_pos = pos;
        }
    }

    /*!
     * Returns the number of the first line of the block.
     */
    inline qsizetype firstLineNumber() const
    {
        return m_firstLineNumber;
    }

    /*!
     * Set the number of the first line of the block.
     *
     * \a number Line number.
     */
    inline void setFirstLineNumber(qsizetype number)
    {
        m_firstLineNumber = number;
    }

    /*!
     * Returns the number of the last line of the block.
     */
    inline qsizetype lastLineNumber() const
    {
        return m_lastLineNumber;
    }

    /*!
     * Set the number of the last line of the block.
     *
     * \a number Line number.
     */
    inline void setLastLineNumber(qsizetype number)
    {
        m_lastLineNumber = number;
    }

    /*!
     * Returns top parent context.
     */
    inline Context &topContext()
    {
        if (m_parent && m_parent->block()) {
            return m_parent->topContext();
        } else {
            return *this;
        }
    }

    /*!
     * Returns most nested child context.
     */
    inline Context &mostNestedChild()
    {
        if (!m_children.isEmpty() && m_children.back().block()) {
            auto it = std::find_if(m_children.rbegin(), m_children.rend(), [](const auto &c) {
                return (c.block() != nullptr);
            });

            if (it != m_children.rend()) {
                return it->mostNestedChild();
            }
        }

        return *this;
    }

    /*!
     * Returns list delimiter for the given indent.
     *
     * \a indent Indent.
     */
    inline QChar listDelim(qsizetype indent) const
    {
        qsizetype idx = -1;

        for (qsizetype i = 0; i < m_childIndents.size(); ++i) {
            if (indentFromColumn(m_childIndents[i].first + indentFromColumn(indentColumn())) <= indent) {
                idx = i;
            }
        };

        return (idx != -1 && idx <= m_childIndents.size() - 2 ? m_childIndents[idx + 1].second
                                                              : (idx == -1 ? m_listDelimiter : QChar()));
    }

    /*!
     * Set top list delimiter.
     *
     * \a d Delimiter.
     */
    inline void setTopListDelim(QChar d)
    {
        m_listDelimiter = d;
    }

    /*!
     * Returns whether this context is not determined as finished. \sa MD::BlockState::Discard, MD::BlockquoteParser,
     * MD::TableParser.
     */
    inline bool isNotFinished() const
    {
        return m_isNotFinished;
    }

    /*!
     * Set not yet finished flag.
     *
     * \a on State flag.
     */
    inline void setNotFinished(bool on = true)
    {
        m_isNotFinished = on;
    }

    /*!
     * Returns whether this context should be discarded. \sa MD::BlockState::Discard, MD::BlockquoteParser,
     * MD::TableParser.
     */
    inline bool isDiscardForced() const
    {
        return m_isDiscardForced;
    }

    /*!
     * Set this context to be discarded.
     *
     * \a on State flag.
     */
    inline void setDiscardForced(bool on = true)
    {
        m_isDiscardForced = on;
    }

    /*!
     * Set to not consider indents in MD::Context::indentForCheck() method.
     *
     * \a on State flag.
     */
    inline void setDontConsiderIndents(bool on = true)
    {
        m_dontConsiderIndents = on;
    }

    /*!
     * Returns actual at this time indent for MD::BlockParser::check() method.
     *
     * \a skipChildren Should child indents be considered?
     */
    inline qsizetype indentColumnForCheck(bool skipChildren) const
    {
        return (m_dontConsiderIndents ? 0 : (skipChildren ? indentColumn() : lastChildIndent()));
    }

    /*!
     * Returns whether the given column is in range.
     *
     * \a column Column.
     */
    inline bool isInIndent(qsizetype column) const
    {
        return (column - indentColumnForCheck(false) < 4
                && (column < indentFromColumn(indentColumn()) + 4
                    || (hasChildIndents() ? column > firstChildIndent() : false)));
    }

    /*!
     * Returns item in the document for this context.
     */
    inline Item *item() const
    {
        return m_item;
    }

    /*!
     * Set item in the document for this context.
     *
     * \a i Item.
     */
    inline void setItem(Item *i)
    {
        m_item = i;
    }

private:
    Context *m_parent = nullptr;
    BlockParser *m_block = nullptr;
    Item *m_item = nullptr;
    Children m_children;
    ChildLists m_lists;
    qsizetype m_indent = 0;
    ChildIndents m_childIndents;
    qsizetype m_firstLineNumber = -1;
    qsizetype m_lastLineNumber = -1;
    LineInfo m_lines;
    QSet<qsizetype> m_lazy;
    QChar m_listDelimiter;
    bool m_isNotFinished = false;
    bool m_isDiscardForced = false;
    bool m_dontConsiderIndents = false;
}; // class Context

} /* namespace MD */

#endif // MD4QT_MD_CONTEXT_H_INCLUDED
