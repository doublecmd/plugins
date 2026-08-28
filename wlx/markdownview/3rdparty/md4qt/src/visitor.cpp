/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "visitor.h"

namespace MD
{

//
// Visitor
//

Visitor::Visitor() = default;

Visitor::~Visitor() = default;

void Visitor::process(QSharedPointer<Document> d)
{
    m_anchors.clear();
    m_doc = d;

    for (auto it = m_doc->items().cbegin(), last = m_doc->items().cend(); it != last; ++it) {
        switch ((*it)->type()) {
        case ItemType::Anchor:
            m_anchors.push_back(static_cast<Anchor *>(it->get())->label());
            break;

        default:
            break;
        }
    }

    for (auto it = m_doc->items().cbegin(), last = m_doc->items().cend(); it != last; ++it) {
        if (static_cast<int>((*it)->type()) >= static_cast<int>(ItemType::UserDefined)) {
            onUserDefined(it->get());
        } else {
            switch ((*it)->type()) {
            case ItemType::Heading:
                onHeading(static_cast<Heading *>(it->get()));
                break;

            case ItemType::Paragraph:
                onParagraph(static_cast<Paragraph *>(it->get()), true);
                break;

            case ItemType::Code:
                onCode(static_cast<Code *>(it->get()));
                break;

            case ItemType::Blockquote:
                onBlockquote(static_cast<Blockquote *>(it->get()));
                break;

            case ItemType::List:
                onList(static_cast<List *>(it->get()));
                break;

            case ItemType::Table:
                onTable(static_cast<Table *>(it->get()));
                break;

            case ItemType::Anchor:
                onAnchor(static_cast<Anchor *>(it->get()));
                break;

            case ItemType::RawHtml:
                onRawHtml(static_cast<RawHtml *>(it->get()));
                break;

            case ItemType::HorizontalLine:
                onHorizontalLine(static_cast<HorizontalLine *>(it->get()));
                break;

            default:
                break;
            }
        }
    }
}

void Visitor::onUserDefined(Item *item)
{
    Q_UNUSED(item)
}

void Visitor::onParagraph(Paragraph *p,
                          bool wrap,
                          bool skipOpeningWrap)
{
    Q_UNUSED(wrap)
    Q_UNUSED(skipOpeningWrap)

    qsizetype l = (!p->items().empty() ? p->items().at(0)->startLine() : -1);

    for (auto it = p->items().begin(), last = p->items().end(); it != last; ++it) {
        if ((*it)->startLine() != l) {
            onAddLineEnding();
        }

        l = (*it)->endLine();

        if (static_cast<int>((*it)->type()) >= static_cast<int>(ItemType::UserDefined)) {
            onUserDefined(it->get());
        } else {
            switch ((*it)->type()) {
            case ItemType::Text:
                onText(static_cast<Text *>(it->get()));
                break;

            case ItemType::Code:
                onInlineCode(static_cast<Code *>(it->get()));
                break;

            case ItemType::Link:
                onLink(static_cast<Link *>(it->get()));
                break;

            case ItemType::Image:
                onImage(static_cast<Image *>(it->get()));
                break;

            case ItemType::Math:
                onMath(static_cast<Math *>(it->get()));
                break;

            case ItemType::LineBreak:
                onLineBreak(static_cast<LineBreak *>(it->get()));
                break;

            case ItemType::FootnoteRef:
                onFootnoteRef(static_cast<FootnoteRef *>(it->get()));
                break;

            case ItemType::RawHtml:
                onRawHtml(static_cast<RawHtml *>(it->get()));
                break;

            // LCOV_EXCL_START
            default:
                break;
                // LCOV_EXCL_STOP
            }
        }
    }
}

void Visitor::onBlockquote(Blockquote *b)
{
    for (auto it = b->items().cbegin(), last = b->items().cend(); it != last; ++it) {
        if (static_cast<int>((*it)->type()) >= static_cast<int>(ItemType::UserDefined)) {
            onUserDefined(it->get());
        } else {
            switch ((*it)->type()) {
            case ItemType::Heading:
                onHeading(static_cast<Heading *>(it->get()));
                break;

            case ItemType::Paragraph:
                onParagraph(static_cast<Paragraph *>(it->get()), true);
                break;

            case ItemType::Code:
                onCode(static_cast<Code *>(it->get()));
                break;

            case ItemType::Blockquote:
                onBlockquote(static_cast<Blockquote *>(it->get()));
                break;

            case ItemType::List:
                onList(static_cast<List *>(it->get()));
                break;

            case ItemType::Table:
                onTable(static_cast<Table *>(it->get()));
                break;

            case ItemType::HorizontalLine:
                onHorizontalLine(static_cast<HorizontalLine *>(it->get()));
                break;

            case ItemType::RawHtml:
                onRawHtml(static_cast<RawHtml *>(it->get()));
                break;

            // LCOV_EXCL_START
            default:
                break;
                // LCOV_EXCL_STOP
            }
        }
    }
}

void Visitor::onListItem(ListItem *i,
                         bool first,
                         bool skipOpeningWrap)
{
    Q_UNUSED(first)

    for (auto it = i->items().cbegin(), last = i->items().cend(); it != last; ++it) {
        if (static_cast<int>((*it)->type()) >= static_cast<int>(ItemType::UserDefined)) {
            onUserDefined(it->get());
        } else {
            switch ((*it)->type()) {
            case ItemType::Heading:
                onHeading(static_cast<Heading *>(it->get()));
                break;

            case ItemType::Paragraph:
                onParagraph(static_cast<Paragraph *>(it->get()), wrapFirstParagraphInListItem(i), skipOpeningWrap);
                break;

            case ItemType::Code:
                onCode(static_cast<Code *>(it->get()));
                break;

            case ItemType::Blockquote:
                onBlockquote(static_cast<Blockquote *>(it->get()));
                break;

            case ItemType::List:
                onList(static_cast<List *>(it->get()));
                break;

            case ItemType::Table:
                onTable(static_cast<Table *>(it->get()));
                break;

            case ItemType::RawHtml:
                onRawHtml(static_cast<RawHtml *>(it->get()));
                break;

            case ItemType::HorizontalLine:
                onHorizontalLine(static_cast<HorizontalLine *>(it->get()));
                break;

            // LCOV_EXCL_START
            default:
                break;
                // LCOV_EXCL_STOP
            }
        }

        skipOpeningWrap = false;
    }
}

void Visitor::onTableCell(TableCell *c)
{
    for (auto it = c->items().cbegin(), last = c->items().cend(); it != last; ++it) {
        if (static_cast<int>((*it)->type()) >= static_cast<int>(ItemType::UserDefined)) {
            onUserDefined(it->get());
        } else {
            switch ((*it)->type()) {
            case ItemType::Text:
                onText(static_cast<Text *>(it->get()));
                break;

            case ItemType::Code:
                onInlineCode(static_cast<Code *>(it->get()));
                break;

            case ItemType::Link:
                onLink(static_cast<Link *>(it->get()));
                break;

            case ItemType::Image:
                onImage(static_cast<Image *>(it->get()));
                break;

            case ItemType::FootnoteRef:
                onFootnoteRef(static_cast<FootnoteRef *>(it->get()));
                break;

            case ItemType::RawHtml:
                onRawHtml(static_cast<RawHtml *>(it->get()));
                break;

            case ItemType::Math:
                onMath(static_cast<Math *>(it->get()));
                break;

            // LCOV_EXCL_START
            default:
                break;
                // LCOV_EXCL_STOP
            }
        }
    }
}

void Visitor::onFootnote(Footnote *f)
{
    for (auto it = f->items().cbegin(), last = f->items().cend(); it != last; ++it) {
        if (static_cast<int>((*it)->type()) >= static_cast<int>(ItemType::UserDefined)) {
            onUserDefined(it->get());
        } else {
            switch ((*it)->type()) {
            case ItemType::Heading:
                onHeading(static_cast<Heading *>(it->get()));
                break;

            case ItemType::Paragraph:
                onParagraph(static_cast<Paragraph *>(it->get()), true);
                break;

            case ItemType::Code:
                onCode(static_cast<Code *>(it->get()));
                break;

            case ItemType::Blockquote:
                onBlockquote(static_cast<Blockquote *>(it->get()));
                break;

            case ItemType::List:
                onList(static_cast<List *>(it->get()));
                break;

            case ItemType::Table:
                onTable(static_cast<Table *>(it->get()));
                break;

            case ItemType::RawHtml:
                onRawHtml(static_cast<RawHtml *>(it->get()));
                break;

            case ItemType::HorizontalLine:
                onHorizontalLine(static_cast<HorizontalLine *>(it->get()));
                break;

            // LCOV_EXCL_START
            default:
                break;
                // LCOV_EXCL_STOP
            }
        }
    }
}

bool Visitor::wrapFirstParagraphInListItem(ListItem *i) const
{
    return (i->items().size() > 1 && i->items().at(1)->type() != ItemType::List);
}

} /* namespace MD */
