/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "algo.h"

namespace MD
{

namespace details
{

//
// AlgoVisitor
//

AlgoVisitor::AlgoVisitor(unsigned int mnl,
                         const QVector<ItemType> &t,
                         ItemFunctor f)
    : m_maxNestingLevel(mnl)
    , m_types(t)
    , m_func(std::move(f))
{
}

AlgoVisitor::~AlgoVisitor() = default;

void AlgoVisitor::walk(QSharedPointer<Document> doc)
{
    this->process(doc);

    onFootnotes(doc);
}

void AlgoVisitor::onAddLineEnding()
{
}

void AlgoVisitor::onUserDefined(Item *i)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(i->type())) {
        m_func(i);
    }
}

void AlgoVisitor::onText(Text *t)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Text)) {
        m_func(t);
    }
}

void AlgoVisitor::onMath(Math *m)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Math)) {
        m_func(m);
    }
}

void AlgoVisitor::onLineBreak(LineBreak *l)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::LineBreak)) {
        m_func(l);
    }
}

void AlgoVisitor::onParagraph(Paragraph *p,
                              bool,
                              bool skipOpeningWrap)
{
    Q_UNUSED(skipOpeningWrap)

    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Paragraph)) {
        m_func(p);
    }

    if (inc.nextAllowed()) {
        Visitor::onParagraph(p, true, skipOpeningWrap);
    }
}

void AlgoVisitor::onHeading(Heading *h)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Heading)) {
        m_func(h);
    }

    if (h->text() && !h->text()->isEmpty() && inc.nextAllowed()) {
        onParagraph(h->text().get(), true);
    }
}

void AlgoVisitor::onCode(Code *c)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Code)) {
        m_func(c);
    }
}

void AlgoVisitor::onInlineCode(Code *c)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Code)) {
        m_func(c);
    }
}

void AlgoVisitor::onBlockquote(Blockquote *b)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Blockquote)) {
        m_func(b);
    }

    if (inc.nextAllowed()) {
        Visitor::onBlockquote(b);
    }
}

void AlgoVisitor::onList(List *l)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::List)) {
        m_func(l);
    }

    for (auto it = l->items().cbegin(), last = l->items().cend(); it != last; ++it) {
        if ((*it)->type() == ItemType::ListItem && inc.nextAllowed()) {
            onListItem(static_cast<ListItem *>(it->get()), true);
        }
    }
}

void AlgoVisitor::onTable(Table *t)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Table)) {
        m_func(t);
    }

    if (!t->isEmpty() && inc.nextAllowed()) {
        int columns = 0;

        for (auto th = (*t->rows().cbegin())->cells().cbegin(), last = (*t->rows().cbegin())->cells().cend();
             th != last;
             ++th) {
            this->onTableCell(th->get());

            ++columns;
        }

        for (auto r = std::next(t->rows().cbegin()), rlast = t->rows().cend(); r != rlast; ++r) {
            int i = 0;

            for (auto c = (*r)->cells().cbegin(), clast = (*r)->cells().cend(); c != clast; ++c) {
                this->onTableCell(c->get());

                ++i;

                if (i == columns) {
                    break;
                }
            }
        }
    }
}

void AlgoVisitor::onAnchor(Anchor *a)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Anchor)) {
        m_func(a);
    }
}

void AlgoVisitor::onRawHtml(RawHtml *h)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::RawHtml)) {
        m_func(h);
    }
}

void AlgoVisitor::onHorizontalLine(HorizontalLine *l)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::HorizontalLine)) {
        m_func(l);
    }
}

void AlgoVisitor::onLink(Link *l)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Link)) {
        m_func(l);
    }

    if (inc.nextAllowed()) {
        if (!l->img()->isEmpty()) {
            onImage(l->img().get());
        } else if (l->p() && !l->p()->isEmpty()) {
            onParagraph(l->p().get(), false);
        }
    }
}

void AlgoVisitor::onImage(Image *i)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Image)) {
        m_func(i);
    }
}

void AlgoVisitor::onFootnoteRef(FootnoteRef *ref)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::FootnoteRef)) {
        m_func(ref);
    }
}

void AlgoVisitor::onListItem(ListItem *i,
                             bool,
                             bool skipOpeningWrap)
{
    Q_UNUSED(skipOpeningWrap)

    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::ListItem)) {
        m_func(i);
    }

    if (inc.nextAllowed()) {
        Visitor::onListItem(i, true);
    }
}

void AlgoVisitor::onTableCell(TableCell *c)
{
    Visitor::onTableCell(c);
}

void AlgoVisitor::onFootnote(Footnote *f)
{
    IncrementNestingLevel inc(m_currentNestingLevel, m_maxNestingLevel, m_types);

    if (inc.allowed(ItemType::Footnote)) {
        m_func(f);
    }

    if (inc.nextAllowed()) {
        Visitor::onFootnote(f);
    }
}

void AlgoVisitor::onFootnotes(QSharedPointer<Document> doc)
{
    for (const auto &f : doc->footnotesMap()) {
        this->onFootnote(f.get());
    }
}

//
// AlgoVisitor::IncrementNestingLevel
//

AlgoVisitor::IncrementNestingLevel::IncrementNestingLevel(unsigned int &l,
                                                          unsigned int m,
                                                          const QVector<ItemType> &t)
    : m_level(l)
    , m_maxNestingLevel(m)
    , m_types(t)
{
    ++m_level;
}

AlgoVisitor::IncrementNestingLevel::~IncrementNestingLevel()
{
    --m_level;
}

bool AlgoVisitor::IncrementNestingLevel::allowed(ItemType t) const
{
    return ((m_maxNestingLevel == 0 || m_level <= m_maxNestingLevel)
            && std::find(m_types.cbegin(), m_types.cend(), t) != m_types.cend());
}

bool AlgoVisitor::IncrementNestingLevel::nextAllowed() const
{
    return (m_maxNestingLevel == 0 || m_level + 1 <= m_maxNestingLevel);
}

} /* namespace details */

void forEach(const QVector<ItemType> &types,
             QSharedPointer<Document> doc,
             ItemFunctor func,
             unsigned int maxNestingLevel)
{
    details::AlgoVisitor v(maxNestingLevel, types, func);

    v.walk(doc);
}

} /* namespace MD */
