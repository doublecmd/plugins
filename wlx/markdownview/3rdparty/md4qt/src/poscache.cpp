/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#include "poscache.h"

// Qt include.
#include <QScopedValueRollback>

// C++ include.
#include <algorithm>
#include <cassert>

namespace MD
{

namespace details
{

//
// PosRange
//

PosRange::PosRange(qsizetype startColumn,
                   qsizetype startLine,
                   qsizetype endColumn,
                   qsizetype endLine)
    : m_startColumn(startColumn)
    , m_startLine(startLine)
    , m_endColumn(endColumn)
    , m_endLine(endLine)
{
}

PosRange::PosRange(qsizetype startColumn,
                   qsizetype startLine,
                   qsizetype endColumn,
                   qsizetype endLine,
                   Item *item)
    : m_startColumn(startColumn)
    , m_startLine(startLine)
    , m_endColumn(endColumn)
    , m_endLine(endLine)
    , m_item(item)
{
}

PosRange::PosRange(qsizetype startColumn,
                   qsizetype startLine,
                   qsizetype endColumn,
                   qsizetype endLine,
                   Item *item,
                   const QVector<QSharedPointer<PosRange>> &children)
    : m_startColumn(startColumn)
    , m_startLine(startLine)
    , m_endColumn(endColumn)
    , m_endLine(endLine)
    , m_item(item)
    , m_children(children)
{
}

bool PosRange::isValidPos() const
{
    return m_startColumn > -1 && m_startLine > -1 && m_endColumn > -1 && m_endLine > -1;
}

bool operator==(const PosRange &l,
                const PosRange &r)
{
    return (l.m_startLine <= r.m_endLine
            && l.m_endLine >= r.m_startLine
            && (l.m_startLine == r.m_endLine && l.m_endLine == r.m_startLine
                    ? l.m_endColumn >= r.m_startColumn && l.m_startColumn <= r.m_endColumn
                    : true));
}

bool operator<(const PosRange &l,
               const PosRange &r)
{
    return (l.m_endLine < r.m_startLine || (l.m_endLine == r.m_startLine && l.m_endColumn < r.m_startColumn));
}

} /* namespace details */

bool comparePosRangeLower(const QSharedPointer<details::PosRange> &ptr,
                          const details::PosRange &range)
{
    return (*ptr < range);
}

bool comparePosRangeUpper(const details::PosRange &range,
                          const QSharedPointer<details::PosRange> &ptr)
{
    return (range < *ptr);
}

//
// PosCache
//

PosCache::PosCache() = default;

PosCache::~PosCache() = default;

void PosCache::initialize(QSharedPointer<MD::Document> doc)
{
    m_cache.clear();

    if (doc) {
        Visitor::process(doc);

        for (auto it = doc->footnotesMap().cbegin(), last = doc->footnotesMap().cend(); it != last; ++it) {
            onFootnote(it->get());
        }

        for (auto it = doc->labeledLinks().cbegin(), last = doc->labeledLinks().cend(); it != last; ++it) {
            onReferenceLink(it->get());
        }
    }
}

PosCache::Items PosCache::findFirstInCache(const MD::WithPosition &pos) const
{
    Items res;

    details::PosRange tmp{pos.startColumn(), pos.startLine(), pos.endColumn(), pos.endLine()};

    findFirstInCache(m_cache, tmp, res);

    return res;
}

details::PosRange *PosCache::findInCache(const QVector<QSharedPointer<details::PosRange>> &vec,
                                         const details::PosRange &pos,
                                         bool ordered) const
{
    if (!m_currentItems.empty()) {
        return m_currentItems.top().get();
    } else if (ordered) {
        return nullptr;
    } else {
        const auto it = std::lower_bound(vec.begin(), vec.end(), pos, comparePosRangeLower);

        if (it != vec.end() && *it->get() == pos) {
            if (!it->get()->m_children.empty()) {
                auto nested = findInCache(it->get()->m_children, pos);

                return (nested ? nested : it->get());
            } else {
                return it->get();
            }
        } else {
            return nullptr;
        }
    }
}

void PosCache::findFirstInCache(const QVector<QSharedPointer<details::PosRange>> &vec,
                                const details::PosRange &pos,
                                Items &res) const
{
    const auto it = std::lower_bound(vec.begin(), vec.end(), pos, comparePosRangeLower);

    if (it != vec.end() && *it->get() == pos) {
        res.push_back(it->get()->m_item);

        if (!it->get()->m_children.empty()) {
            findFirstInCache(it->get()->m_children, pos, res);
        }
    }
}

void PosCache::insertInCache(const details::PosRange &item,
                             bool sort,
                             bool insertInStack)
{
    const auto insertInStackFunc = [this, insertInStack](QSharedPointer<details::PosRange> item) {
        if (insertInStack) {
            this->m_currentItems.push(item);
        }
    };

    static const auto makeSharedPosRange = [](const details::PosRange &range) -> QSharedPointer<details::PosRange> {
        return QSharedPointer<details::PosRange>::create(range.m_startColumn,
                                                         range.m_startLine,
                                                         range.m_endColumn,
                                                         range.m_endLine,
                                                         range.m_item,
                                                         range.m_children);
    };

    if (!m_skipInCache) {
        assert(item.isValidPos());

        auto pos = findInCache(m_cache, item, !sort);

        if (pos) {
            pos->m_children.push_back(makeSharedPosRange(item));

            insertInStackFunc(pos->m_children.back());
        } else {
            if (sort) {
                const auto it = std::upper_bound(m_cache.begin(), m_cache.end(), item, comparePosRangeUpper);

                if (it != m_cache.end()) {
                    insertInStackFunc(*m_cache.insert(it, makeSharedPosRange(item)));
                } else {
                    m_cache.push_back(makeSharedPosRange(item));

                    insertInStackFunc(m_cache.back());
                }
            } else {
                m_cache.push_back(makeSharedPosRange(item));

                insertInStackFunc(m_cache.back());
            }
        }
    }
}

void PosCache::onUserDefined(Item *i)
{
    details::PosRange r{i->startColumn(), i->startLine(), i->endColumn(), i->endLine(), i};

    insertInCache(r);
}

void PosCache::onReferenceLink(Link *l)
{
    details::PosRange r{l->startColumn(), l->startLine(), l->endColumn(), l->endLine(), l};

    insertInCache(r, true);
}

void PosCache::onAddLineEnding()
{
}

void PosCache::onText(Text *t)
{
    details::PosRange r{t->openStyles().empty() ? t->startColumn() : t->openStyles().front().startColumn(),
                        t->startLine(),
                        t->closeStyles().empty() ? t->endColumn() : t->closeStyles().back().endColumn(),
                        t->endLine(),
                        t};

    insertInCache(r);
}

void PosCache::onMath(Math *m)
{
    auto startColumn = m->startDelim().startColumn();
    auto startLine = m->startDelim().startLine();
    auto endColumn = m->endDelim().endColumn();
    auto endLine = m->endDelim().endLine();

    if (!m->openStyles().empty()) {
        startColumn = m->openStyles().front().startColumn();
        startLine = m->openStyles().front().startLine();
    }

    if (!m->closeStyles().empty()) {
        endColumn = m->closeStyles().back().endColumn();
        endLine = m->closeStyles().back().endLine();
    }

    details::PosRange r{startColumn, startLine, endColumn, endLine, m};

    insertInCache(r);
}

void PosCache::onLineBreak(LineBreak *)
{
}

void PosCache::onParagraph(Paragraph *p,
                           bool wrap,
                           bool skipOpeningWrap)
{
    details::PosRange r{p->startColumn(), p->startLine(), p->endColumn(), p->endLine(), p};

    insertInCache(r, false, true);

    Visitor::onParagraph(p, wrap, skipOpeningWrap);

    if (!m_skipInCache) {
        m_currentItems.pop();
    }
}

void PosCache::onHeading(Heading *h)
{
    details::PosRange r{h->startColumn(), h->startLine(), h->endColumn(), h->endLine(), h};

    insertInCache(r, false, true);

    if (h->text() && !h->text()->isEmpty()) {
        onParagraph(h->text().get(), false);
    }

    m_currentItems.pop();
}

void PosCache::onCode(Code *c)
{
    auto startColumn = c->isFensedCode() ? c->startDelim().startColumn() : c->startColumn();
    auto startLine = c->isFensedCode() ? c->startDelim().startLine() : c->startLine();
    auto endColumn = c->isFensedCode() && c->endDelim().endColumn() > -1 ? c->endDelim().endColumn() : c->endColumn();
    auto endLine = c->isFensedCode() && c->endDelim().endLine() > -1 ? c->endDelim().endLine() : c->endLine();

    details::PosRange r{startColumn, startLine, endColumn, endLine, c};

    insertInCache(r);
}

void PosCache::onInlineCode(Code *c)
{
    auto startColumn = c->startDelim().startColumn();
    auto startLine = c->startDelim().startLine();
    auto endColumn = c->endDelim().endColumn();
    auto endLine = c->endDelim().endLine();

    if (!c->openStyles().empty()) {
        startColumn = c->openStyles().front().startColumn();
        startLine = c->openStyles().front().startLine();
    }

    if (!c->closeStyles().empty()) {
        endColumn = c->closeStyles().back().endColumn();
        endLine = c->closeStyles().back().endLine();
    }

    details::PosRange r{startColumn, startLine, endColumn, endLine, c};

    insertInCache(r);
}

void PosCache::onBlockquote(Blockquote *b)
{
    details::PosRange r{b->startColumn(), b->startLine(), b->endColumn(), b->endLine(), b};

    insertInCache(r, false, true);

    Visitor::onBlockquote(b);

    m_currentItems.pop();
}

void PosCache::onList(List *l)
{
    bool first = true;

    details::PosRange r{l->startColumn(), l->startLine(), l->endColumn(), l->endLine(), l};

    insertInCache(r, false, true);

    for (auto it = l->items().cbegin(), last = l->items().cend(); it != last; ++it) {
        if ((*it)->type() == ItemType::ListItem) {
            onListItem(static_cast<ListItem *>(it->get()), first);

            first = false;
        }
    }

    m_currentItems.pop();
}

void PosCache::onTable(Table *t)
{
    details::PosRange r{t->startColumn(), t->startLine(), t->endColumn(), t->endLine(), t};

    insertInCache(r, false, true);

    if (!t->isEmpty()) {
        int columns = 0;

        for (auto th = (*t->rows().cbegin())->cells().cbegin(), last = (*t->rows().cbegin())->cells().cend();
             th != last;
             ++th) {
            Visitor::onTableCell(th->get());

            ++columns;
        }

        for (auto r = std::next(t->rows().cbegin()), rlast = t->rows().cend(); r != rlast; ++r) {
            int i = 0;

            for (auto c = (*r)->cells().cbegin(), clast = (*r)->cells().cend(); c != clast; ++c) {
                Visitor::onTableCell(c->get());

                ++i;

                if (i == columns) {
                    break;
                }
            }
        }
    }

    m_currentItems.pop();
}

void PosCache::onAnchor(Anchor *)
{
}

void PosCache::onRawHtml(RawHtml *h)
{
    details::PosRange r{h->startColumn(), h->startLine(), h->endColumn(), h->endLine(), h};

    insertInCache(r);
}

void PosCache::onHorizontalLine(HorizontalLine *l)
{
    details::PosRange r{l->startColumn(), l->startLine(), l->endColumn(), l->endLine(), l};

    insertInCache(r);
}

void PosCache::onLink(Link *l)
{
    auto startColumn = l->startColumn();
    auto startLine = l->startLine();
    auto endColumn = l->endColumn();
    auto endLine = l->endLine();

    if (!l->openStyles().empty()) {
        startColumn = l->openStyles().front().startColumn();
        startLine = l->openStyles().front().startLine();
    }

    if (!l->closeStyles().empty()) {
        endColumn = l->closeStyles().back().endColumn();
        endLine = l->closeStyles().back().endLine();
    }

    details::PosRange r{startColumn, startLine, endColumn, endLine, l};

    insertInCache(r);

    if (l->p()) {
        QScopedValueRollback skipInCacheRollback(m_skipInCache, true);
        onParagraph(l->p().get(), true);
    }
}

void PosCache::onImage(Image *i)
{
    auto startColumn = i->startColumn();
    auto startLine = i->startLine();
    auto endColumn = i->endColumn();
    auto endLine = i->endLine();

    if (!i->openStyles().empty()) {
        startColumn = i->openStyles().front().startColumn();
        startLine = i->openStyles().front().startLine();
    }

    if (!i->closeStyles().empty()) {
        endColumn = i->closeStyles().back().endColumn();
        endLine = i->closeStyles().back().endLine();
    }

    details::PosRange r{startColumn, startLine, endColumn, endLine, i};

    insertInCache(r);

    if (i->p()) {
        QScopedValueRollback skipInCacheRollback(m_skipInCache, true);
        onParagraph(i->p().get(), true);
    }
}

void PosCache::onFootnoteRef(FootnoteRef *ref)
{
    auto startColumn = ref->startColumn();
    auto startLine = ref->startLine();
    auto endColumn = ref->endColumn();
    auto endLine = ref->endLine();

    if (!ref->openStyles().empty()) {
        startColumn = ref->openStyles().front().startColumn();
        startLine = ref->openStyles().front().startLine();
    }

    if (!ref->closeStyles().empty()) {
        endColumn = ref->closeStyles().back().endColumn();
        endLine = ref->closeStyles().back().endLine();
    }

    details::PosRange r{startColumn, startLine, endColumn, endLine, ref};

    insertInCache(r);
}

void PosCache::onFootnote(Footnote *f)
{
    details::PosRange r{f->startColumn(), f->startLine(), f->endColumn(), f->endLine(), f};

    insertInCache(r, true, true);

    Visitor::onFootnote(f);

    m_currentItems.pop();
}

void PosCache::onListItem(ListItem *l,
                          bool first,
                          bool skipOpeningWrap)
{
    details::PosRange r{l->startColumn(), l->startLine(), l->endColumn(), l->endLine(), l};

    insertInCache(r, false, true);

    Visitor::onListItem(l, first, skipOpeningWrap);

    m_currentItems.pop();
}

} /* namespace MD */
