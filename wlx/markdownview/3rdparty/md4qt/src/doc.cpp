/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "doc.h"

namespace MD
{

//
// WithPosition
//

WithPosition::WithPosition() = default;

WithPosition::~WithPosition() = default;

WithPosition::WithPosition(qsizetype startColumn,
                           qsizetype startLine,
                           qsizetype endColumn,
                           qsizetype endLine)
    : m_startColumn(startColumn)
    , m_startLine(startLine)
    , m_endColumn(endColumn)
    , m_endLine(endLine)
{
}

void WithPosition::applyPositions(const WithPosition &other)
{
    if (this != &other) {
        *this = other;
    }
}

qsizetype WithPosition::startColumn() const
{
    return m_startColumn;
}

qsizetype WithPosition::startLine() const
{
    return m_startLine;
}

qsizetype WithPosition::endColumn() const
{
    return m_endColumn;
}

qsizetype WithPosition::endLine() const
{
    return m_endLine;
}

void WithPosition::setStartColumn(qsizetype c)
{
    m_startColumn = c;
}

void WithPosition::setStartLine(qsizetype l)
{
    m_startLine = l;
}

void WithPosition::setEndColumn(qsizetype c)
{
    m_endColumn = c;
}

void WithPosition::setEndLine(qsizetype l)
{
    m_endLine = l;
}

bool WithPosition::isNullPositions() const
{
    return (m_startColumn == -1 || m_startLine == -1 || m_endColumn == -1 || m_endLine == -1);
}

bool operator==(const WithPosition &l,
                const WithPosition &r)
{
    return (l.startColumn() == r.startColumn()
            && l.startLine() == r.startLine()
            && l.endColumn() == r.endColumn()
            && l.endLine() == r.endLine());
}

//
// Item
//

Item::Item() = default;

Item::~Item() = default;

//
// StyleDelim
//

StyleDelim::StyleDelim(int s,
                       qsizetype startColumn,
                       qsizetype startLine,
                       qsizetype endColumn,
                       qsizetype endLine)
    : WithPosition(startColumn,
                   startLine,
                   endColumn,
                   endLine)
    , m_style(s)
{
}

StyleDelim::~StyleDelim() = default;

int StyleDelim::style() const
{
    return m_style;
}

void StyleDelim::setStyle(int t)
{
    m_style = t;
}

bool operator==(const StyleDelim &l,
                const StyleDelim &r)
{
    return (static_cast<WithPosition>(l) == static_cast<WithPosition>(r) && l.style() == r.style());
}

//
// ItemWithOpts
//

ItemWithOpts::ItemWithOpts() = default;

ItemWithOpts::~ItemWithOpts() = default;

void ItemWithOpts::applyItemWithOpts(const ItemWithOpts &other)
{
    if (this != &other) {
        WithPosition::applyPositions(other);
        m_opts = other.m_opts;
        m_openStyles = other.m_openStyles;
        m_closeStyles = other.m_closeStyles;
    }
}

int ItemWithOpts::opts() const
{
    return m_opts;
}

void ItemWithOpts::setOpts(int o)
{
    m_opts = o;
}

const ItemWithOpts::Styles &ItemWithOpts::openStyles() const
{
    return m_openStyles;
}

ItemWithOpts::Styles &ItemWithOpts::openStyles()
{
    return m_openStyles;
}

const ItemWithOpts::Styles &ItemWithOpts::closeStyles() const
{
    return m_closeStyles;
}

ItemWithOpts::Styles &ItemWithOpts::closeStyles()
{
    return m_closeStyles;
}

//
// PageBreak
//

PageBreak::PageBreak() = default;

PageBreak::~PageBreak() = default;

ItemType PageBreak::type() const
{
    return ItemType::PageBreak;
}

QSharedPointer<Item> PageBreak::clone(Document *doc) const
{
    Q_UNUSED(doc)

    return QSharedPointer<PageBreak>::create();
}

//
// HorizontalLine
//

HorizontalLine::HorizontalLine() = default;

HorizontalLine::~HorizontalLine() = default;

ItemType HorizontalLine::type() const
{
    return ItemType::HorizontalLine;
}

QSharedPointer<Item> HorizontalLine::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto h = QSharedPointer<HorizontalLine>::create();
    h->applyPositions(*this);

    return h;
}

//
// Anchor
//

Anchor::Anchor(const QString &l)
    : m_label(l)
{
}

Anchor::~Anchor() = default;

QSharedPointer<Item> Anchor::clone(Document *doc) const
{
    Q_UNUSED(doc)

    return QSharedPointer<Anchor>::create(m_label);
}

ItemType Anchor::type() const
{
    return ItemType::Anchor;
}

const QString &Anchor::label() const
{
    return m_label;
}

//
// RawHtml
//

RawHtml::RawHtml() = default;

RawHtml::~RawHtml() = default;

QSharedPointer<Item> RawHtml::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto h = QSharedPointer<RawHtml>::create();
    h->applyItemWithOpts(*this);
    h->setText(m_text);

    return h;
}

ItemType RawHtml::type() const
{
    return ItemType::RawHtml;
}

const QString &RawHtml::text() const
{
    return m_text;
}

void RawHtml::setText(const QString &t)
{
    m_text = t;
}

//
// Text
//

Text::Text() = default;

Text::~Text() = default;

void Text::applyText(const Text &t)
{
    if (this != &t) {
        ItemWithOpts::applyItemWithOpts(t);
        setText(t.text());
    }
}

QSharedPointer<Item> Text::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto t = QSharedPointer<Text>::create();
    t->applyText(*this);

    return t;
}

ItemType Text::type() const
{
    return ItemType::Text;
}

const QString &Text::text() const
{
    return m_text;
}

void Text::setText(const QString &t)
{
    m_text = t;
}

//
// LineBreak
//

LineBreak::LineBreak() = default;

LineBreak::~LineBreak() = default;

QSharedPointer<Item> LineBreak::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto b = QSharedPointer<LineBreak>::create();
    b->applyText(*this);

    return b;
}

ItemType LineBreak::type() const
{
    return ItemType::LineBreak;
}

//
// Block
//

Block::Block() = default;

Block::~Block() = default;

void Block::applyBlock(const Block &other,
                       Document *doc)
{
    if (this != &other) {
        WithPosition::applyPositions(other);

        m_items.clear();

        for (const auto &i : other.items()) {
            appendItem(i->clone(doc));
        }
    }
}

const Block::Items &Block::items() const
{
    return m_items;
}

void Block::insertItem(qsizetype idx,
                       ItemSharedPointer i)
{
    m_items.insert(m_items.cbegin() + idx, i);
}

void Block::appendItem(ItemSharedPointer i)
{
    m_items.push_back(i);
}

void Block::removeItemAt(qsizetype idx)
{
    if (idx >= 0 && idx < static_cast<qsizetype>(m_items.size())) {
        m_items.erase(m_items.cbegin() + idx);
    }
}

Block::ItemSharedPointer Block::getItemAt(qsizetype idx) const
{
    return m_items.at(idx);
}

bool Block::isEmpty() const
{
    return m_items.empty();
}

//
// Paragraph
//

Paragraph::Paragraph() = default;

Paragraph::~Paragraph() = default;

QSharedPointer<Item> Paragraph::clone(Document *doc) const
{
    auto p = QSharedPointer<Paragraph>::create();
    p->applyBlock(*this, doc);

    return p;
}

ItemType Paragraph::type() const
{
    return ItemType::Paragraph;
}

//
// Heading
//

Heading::Heading()
    : m_text(new Paragraph)
{
}

Heading::~Heading() = default;

QSharedPointer<Item> Heading::clone(Document *doc) const
{
    auto h = QSharedPointer<Heading>::create();
    h->applyPositions(*this);
    h->setText(m_text->clone(doc).staticCast<Paragraph>());
    h->setLevel(m_level);
    h->setLabel(m_label);
    h->setDelims(m_delims);
    h->setLabelPos(m_labelPos);
    h->setLabelVariants(m_labelVariants);

    if (doc && isLabeled()) {
        for (const auto &label : std::as_const(m_labelVariants)) {
            doc->insertLabeledHeading(label, h);
        }
    }

    return h;
}

ItemType Heading::type() const
{
    return ItemType::Heading;
}

Heading::ParagraphSharedPointer Heading::text() const
{
    return m_text;
}

void Heading::setText(ParagraphSharedPointer t)
{
    m_text = t;
}

int Heading::level() const
{
    return m_level;
}

void Heading::setLevel(int l)
{
    m_level = l;
}

bool Heading::isLabeled() const
{
    return m_label.size() > 0;
}

const QString &Heading::label() const
{
    return m_label;
}

void Heading::setLabel(const QString &l)
{
    m_label = l;
}

const Heading::Delims &Heading::delims() const
{
    return m_delims;
}

void Heading::setDelims(const Delims &d)
{
    m_delims = d;
}

const WithPosition &Heading::labelPos() const
{
    return m_labelPos;
}

void Heading::setLabelPos(const WithPosition &p)
{
    m_labelPos = p;
}

const Heading::LabelsVector &Heading::labelVariants() const
{
    return m_labelVariants;
}

Heading::LabelsVector &Heading::labelVariants()
{
    return m_labelVariants;
}

void Heading::setLabelVariants(const LabelsVector &vars)
{
    m_labelVariants = vars;
}

//
// Blockquote
//

Blockquote::Blockquote() = default;

Blockquote::~Blockquote() = default;

QSharedPointer<Item> Blockquote::clone(Document *doc) const
{
    auto b = QSharedPointer<Blockquote>::create();
    b->applyBlock(*this, doc);
    b->delims() = m_delims;

    return b;
}

ItemType Blockquote::type() const
{
    return ItemType::Blockquote;
}

const Blockquote::Delims &Blockquote::delims() const
{
    return m_delims;
}

Blockquote::Delims &Blockquote::delims()
{
    return m_delims;
}

//
// ListItem
//

ListItem::ListItem() = default;

ListItem::~ListItem() = default;

QSharedPointer<Item> ListItem::clone(Document *doc) const
{
    auto l = QSharedPointer<ListItem>::create();
    l->applyBlock(*this, doc);
    l->setListType(m_listType);
    l->setOrderedListPreState(m_orderedListState);
    l->setStartNumber(m_startNumber);
    l->setTaskList(m_isTaskList);
    l->setChecked(m_isChecked);
    l->setDelim(m_delim);
    l->setTaskDelim(m_taskDelim);

    return l;
}

ItemType ListItem::type() const
{
    return ItemType::ListItem;
}

ListItem::ListType ListItem::listType() const
{
    return m_listType;
}

void ListItem::setListType(ListType t)
{
    m_listType = t;
}

ListItem::OrderedListPreState ListItem::orderedListPreState() const
{
    return m_orderedListState;
}

void ListItem::setOrderedListPreState(OrderedListPreState s)
{
    m_orderedListState = s;
}

int ListItem::startNumber() const
{
    return m_startNumber;
}

void ListItem::setStartNumber(int n)
{
    m_startNumber = n;
}

bool ListItem::isTaskList() const
{
    return m_isTaskList;
}

void ListItem::setTaskList(bool on)
{
    m_isTaskList = on;
}

bool ListItem::isChecked() const
{
    return m_isChecked;
}

void ListItem::setChecked(bool on)
{
    m_isChecked = on;
}

const WithPosition &ListItem::delim() const
{
    return m_delim;
}

void ListItem::setDelim(const WithPosition &d)
{
    m_delim = d;
}

const WithPosition &ListItem::taskDelim() const
{
    return m_taskDelim;
}

void ListItem::setTaskDelim(const WithPosition &d)
{
    m_taskDelim = d;
}

//
// List
//

List::List() = default;

List::~List() = default;

QSharedPointer<Item> List::clone(Document *doc) const
{
    auto l = QSharedPointer<List>::create();
    l->applyBlock(*this, doc);

    return l;
}

ItemType List::type() const
{
    return ItemType::List;
}

//
// LinkBase
//

LinkBase::LinkBase()
    : m_p(new Paragraph)
{
}

LinkBase::~LinkBase() = default;

void LinkBase::applyLinkBase(const LinkBase &other,
                             Document *doc)
{
    if (this != &other) {
        ItemWithOpts::applyItemWithOpts(other);
        setUrl(other.url());
        setTitle(other.title());
        setText(other.text());
        setP(other.p()->clone(doc).staticCast<Paragraph>());
        setTextPos(other.textPos());
        setUrlPos(other.urlPos());
    }
}

const QString &LinkBase::url() const
{
    return m_url;
}

void LinkBase::setUrl(const QString &u)
{
    m_url = u;
}

const QString &LinkBase::title() const
{
    return m_title;
}

void LinkBase::setTitle(const QString &t)
{
    m_title = t;
}

const QString &LinkBase::text() const
{
    return m_text;
}

void LinkBase::setText(const QString &t)
{
    m_text = t;
}

bool LinkBase::isEmpty() const
{
    return m_url.size() <= 0;
}

LinkBase::ParagraphSharedPointer LinkBase::p() const
{
    return m_p;
}

void LinkBase::setP(ParagraphSharedPointer v)
{
    m_p = v;
}

const WithPosition &LinkBase::textPos() const
{
    return m_textPos;
}

void LinkBase::setTextPos(const WithPosition &pos)
{
    m_textPos = pos;
}

const WithPosition &LinkBase::urlPos() const
{
    return m_urlPos;
}

void LinkBase::setUrlPos(const WithPosition &pos)
{
    m_urlPos = pos;
}

//
// Image
//

Image::Image() = default;

Image::~Image() = default;

QSharedPointer<Item> Image::clone(Document *doc) const
{
    auto i = QSharedPointer<Image>::create();
    i->applyLinkBase(*this, doc);

    return i;
}

ItemType Image::type() const
{
    return ItemType::Image;
}

//
// Link
//

Link::Link()
    : LinkBase()
    , m_img(new Image)
{
}

Link::~Link() = default;

QSharedPointer<Item> Link::clone(Document *doc) const
{
    auto l = QSharedPointer<Link>::create();
    l->applyLinkBase(*this, doc);
    l->setImg(m_img->clone(doc).staticCast<Image>());

    return l;
}

ItemType Link::type() const
{
    return ItemType::Link;
}

Link::ImageSharedPointer Link::img() const
{
    return m_img;
}

void Link::setImg(ImageSharedPointer i)
{
    m_img = i;
}

//
// Code
//

Code::Code(const QString &t,
           bool fensedCode,
           bool inl)
    : ItemWithOpts()
    , m_text(t)
    , m_inlined(inl)
    , m_fensed(fensedCode)
{
}

Code::~Code() = default;

void Code::applyCode(const Code &other)
{
    if (this != &other) {
        ItemWithOpts::applyItemWithOpts(other);
        setText(other.text());
        setInline(other.isInline());
        setSyntax(other.syntax());
        setSyntaxPos(other.syntaxPos());
        setStartDelim(other.startDelim());
        setEndDelim(other.endDelim());
        setFensedCode(other.isFensedCode());
    }
}

QSharedPointer<Item> Code::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto c = QSharedPointer<Code>::create(m_text, m_fensed, m_inlined);
    c->applyCode(*this);

    return c;
}

ItemType Code::type() const
{
    return ItemType::Code;
}

const QString &Code::text() const
{
    return m_text;
}

void Code::setText(const QString &t)
{
    m_text = t;
}

bool Code::isInline() const
{
    return m_inlined;
}

void Code::setInline(bool on)
{
    m_inlined = on;
}

const QString &Code::syntax() const
{
    return m_syntax;
}

void Code::setSyntax(const QString &s)
{
    m_syntax = s;
}

const WithPosition &Code::syntaxPos() const
{
    return m_syntaxPos;
}

void Code::setSyntaxPos(const WithPosition &p)
{
    m_syntaxPos = p;
}

const WithPosition &Code::startDelim() const
{
    return m_startDelim;
}

void Code::setStartDelim(const WithPosition &d)
{
    m_startDelim = d;
}

const WithPosition &Code::endDelim() const
{
    return m_endDelim;
}

void Code::setEndDelim(const WithPosition &d)
{
    m_endDelim = d;
}

bool Code::isFensedCode() const
{
    return m_fensed;
}

void Code::setFensedCode(bool on)
{
    m_fensed = on;
}

//
// Math
//

Math::Math()
    : Code({},
           false,
           true)
{
}

Math::~Math() = default;

QSharedPointer<Item> Math::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto m = QSharedPointer<Math>::create();
    m->applyCode(*this);

    return m;
}

ItemType Math::type() const
{
    return ItemType::Math;
}

const QString &Math::expr() const
{
    return Code::text();
}

void Math::setExpr(const QString &e)
{
    Code::setText(e);
}

//
// TableCell
//

TableCell::TableCell() = default;

TableCell::~TableCell() = default;

QSharedPointer<Item> TableCell::clone(Document *doc) const
{
    auto c = QSharedPointer<TableCell>::create();
    c->applyBlock(*this, doc);

    return c;
}

ItemType TableCell::type() const
{
    return ItemType::TableCell;
}

//
// TableRow
//

TableRow::TableRow() = default;

TableRow::~TableRow() = default;

QSharedPointer<Item> TableRow::clone(Document *doc) const
{
    auto t = QSharedPointer<TableRow>::create();
    t->applyPositions(*this);

    for (const auto &c : cells()) {
        t->appendCell(c->clone(doc).staticCast<TableCell>());
    }

    return t;
}

ItemType TableRow::type() const
{
    return ItemType::TableRow;
}

const TableRow::Cells &TableRow::cells() const
{
    return m_cells;
}

void TableRow::appendCell(TableCellSharedPointer c)
{
    m_cells.push_back(c);
}

bool TableRow::isEmpty() const
{
    return m_cells.empty();
}

//
// Table
//

Table::Table() = default;

Table::~Table() = default;

QSharedPointer<Item> Table::clone(Document *doc) const
{
    auto t = QSharedPointer<Table>::create();
    t->applyPositions(*this);

    for (const auto &r : rows()) {
        t->appendRow(r->clone(doc).staticCast<TableRow>());
    }

    for (int i = 0; i < columnsCount(); ++i) {
        t->setColumnAlignment(i, columnAlignment(i));
    }

    return t;
}

ItemType Table::type() const
{
    return ItemType::Table;
}

const Table::Rows &Table::rows() const
{
    return m_rows;
}

void Table::appendRow(TableRowSharedPointer r)
{
    m_rows.push_back(r);
}

Table::Alignment Table::columnAlignment(int idx) const
{
    return m_aligns.at(idx);
}

void Table::setColumnAlignment(int idx,
                               Alignment a)
{
    if (idx + 1 > columnsCount()) {
        m_aligns.push_back(a);
    } else {
        m_aligns[idx] = a;
    }
}

int Table::columnsCount() const
{
    return m_aligns.size();
}

bool Table::isEmpty() const
{
    return (m_aligns.empty() || m_rows.empty());
}

//
// FootnoteRef
//

FootnoteRef::FootnoteRef(const QString &i)
    : m_id(i)
{
}

FootnoteRef::~FootnoteRef() = default;

QSharedPointer<Item> FootnoteRef::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto f = QSharedPointer<FootnoteRef>::create(m_id);
    f->applyText(*this);
    f->setIdPos(m_idPos);

    return f;
}

ItemType FootnoteRef::type() const
{
    return ItemType::FootnoteRef;
}

const QString &FootnoteRef::id() const
{
    return m_id;
}

const WithPosition &FootnoteRef::idPos() const
{
    return m_idPos;
}

void FootnoteRef::setIdPos(const WithPosition &pos)
{
    m_idPos = pos;
}

//
// Footnote
//

Footnote::Footnote() = default;

Footnote::~Footnote() = default;

QSharedPointer<Item> Footnote::clone(Document *doc) const
{
    auto f = QSharedPointer<Footnote>::create();
    f->applyBlock(*this, doc);
    f->setIdPos(m_idPos);

    return f;
}

ItemType Footnote::type() const
{
    return ItemType::Footnote;
}

const WithPosition &Footnote::idPos() const
{
    return m_idPos;
}

void Footnote::setIdPos(const WithPosition &pos)
{
    m_idPos = pos;
}

//
// Document
//

Document::Document() = default;

Document::~Document() = default;

ItemType Document::type() const
{
    return ItemType::Document;
}

QSharedPointer<Item> Document::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto d = QSharedPointer<Document>::create();
    d->applyBlock(*this, d.get());

    for (auto it = m_footnotes.cbegin(), last = m_footnotes.cend(); it != last; ++it) {
        d->insertFootnote(it.key(), it.value()->clone(d.get()).staticCast<Footnote>());
    }

    for (auto it = m_labeledLinks.cbegin(), last = m_labeledLinks.cend(); it != last; ++it) {
        d->insertLabeledLink(it.key(), it.value()->clone(d.get()).staticCast<Link>());
    }

    d->auxLabelsMap() = auxLabelsMap();

    return d;
}

const Document::Footnotes &Document::footnotesMap() const
{
    return m_footnotes;
}

void Document::insertFootnote(const QString &id,
                              FootnoteSharedPointer fn)
{
    m_footnotes.insert(id, fn);
}

const Document::LabeledLinks &Document::labeledLinks() const
{
    return m_labeledLinks;
}

void Document::insertLabeledLink(const QString &label,
                                 LinkSharedPointer lnk)
{
    m_labeledLinks.insert(label, lnk);
}

const Document::LabeledHeadings &Document::labeledHeadings() const
{
    return m_labeledHeadings;
}

void Document::insertLabeledHeading(const QString &label,
                                    HeadingSharedPointer h)
{
    m_labeledHeadings.insert(label, h);
}

Document::AuxLabelsMap &Document::auxLabelsMap()
{
    return m_auxLabelsMap;
}

const Document::AuxLabelsMap &Document::auxLabelsMap() const
{
    return m_auxLabelsMap;
}

} /* namespace MD */
