/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_DOC_H_INCLUDED
#define MD4QT_MD_DOC_H_INCLUDED

// Qt include.
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>
// QtClassHelperMacros (just Q_DISABLE_COPY & friends, split out for
// modularity) only exists from Qt 6.5 onward -- older Qt6 (6.4, as shipped
// by e.g. Ubuntu 24.04) already gets these macros transitively via
// qglobal.h through the QMap/QString/etc. includes above, so guard this
// one instead of hard-requiring a Qt minor version this project doesn't
// otherwise need.
#if __has_include(<QtClassHelperMacros>)
#include <QtClassHelperMacros>
#endif

namespace MD
{

//
// ItemType
//

/*!
 * \enum MD::ItemType
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Enumeration of item types.
 *
 * \value Heading Heading.
 * \value Text Text.
 * \value Paragraph Paragraph.
 * \value LineBreak Line break.
 * \value Blockquote Blockquote.
 * \value ListItem List item.
 * \value List List.
 * \value Link Link.
 * \value Image Image.
 * \value Code Code.
 * \value TableCell Table cell.
 * \value TableRow Table row.
 * \value Table Table.
 * \value FootnoteRef Footnote ref.
 * \value Footnote Footnote.
 * \value Document Document.
 * \value PageBreak Page break.
 * \value Anchor Anchor.
 * \value HorizontalLine Horizontal line.
 * \value RawHtml Raw HTML.
 * \value Math Math expression.
 * \value UserDefined Start item for user-defined types.
 */
enum class ItemType : int {
    Heading = 0,
    Text,
    Paragraph,
    LineBreak,
    Blockquote,
    ListItem,
    List,
    Link,
    Image,
    Code,
    TableCell,
    TableRow,
    Table,
    FootnoteRef,
    Footnote,
    Document,
    PageBreak,
    Anchor,
    HorizontalLine,
    RawHtml,
    Math,
    UserDefined = 255
}; // enum class ItemType

//
// WithPosition
//
/*!
 * \class MD::WithPosition
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Base for any thing with start and end position.
 *
 * Such information may be needed for syntax highlighters, for example.
 *
 * \note Start position of the document has coordinates (0, 0).
 */
class WithPosition
{
public:
    /*!
     * Default constructor.
     *
     * All positions will be set to -1.
     */
    WithPosition();
    virtual ~WithPosition();

    /*!
     * Initializing constructor with all positions.
     *
     * \a startColumn Start column.
     *
     * \a startLine Start line.
     *
     * \a endColumn End column.
     *
     * \a endLine End line.
     */
    WithPosition(qsizetype startColumn,
                 qsizetype startLine,
                 qsizetype endColumn,
                 qsizetype endLine);

    /*!
     * Apply positions to this from other.
     *
     * \a other Positions to apply.
     */
    void applyPositions(const WithPosition &other);

    /*!
     * Returns start column.
     */
    qsizetype startColumn() const;

    /*!
     * Returns start line.
     */
    qsizetype startLine() const;

    /*!
     * Returns end column.
     */
    qsizetype endColumn() const;

    /*!
     * Returns end line.
     */
    qsizetype endLine() const;

    /*!
     * Set start column.
     *
     * \a c New value.
     */
    void setStartColumn(qsizetype c);

    /*!
     * Set start line.
     *
     * \a l New value.
     */
    void setStartLine(qsizetype l);

    /*!
     * Set end column.
     *
     * \a c New value.
     */
    void setEndColumn(qsizetype c);

    /*!
     * Set end line.
     *
     * \a l New value.
     */
    void setEndLine(qsizetype l);

    /*!
     * Returns whether this positions is null (not set).
     */
    bool isNullPositions() const;

private:
    /*!
     * Start column
     */
    qsizetype m_startColumn = -1;
    /*!
     * Start line.
     */
    qsizetype m_startLine = -1;
    /*!
     * End column.
     */
    qsizetype m_endColumn = -1;
    /*!
     * End line.
     */
    qsizetype m_endLine = -1;
}; // class WithPosition

/*!
 * \relates MD::WithPosition
 * \inheaderfile md4qt/doc.h
 *
 * Returns whether both are equal.
 *
 * \a l Left operand.
 *
 * \a r Right operand.
 */
bool operator==(const WithPosition &l,
                const WithPosition &r);

class Document;

//
// Item
//

/*!
 * \class MD::Item
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Base class for item in Markdown document.
 *
 * All items in MD::Document derived from this class. Main thing that this class does
 * is a virtual method MD::Item::type that return type of the item, so when working
 * with MD::Document a developer can know what type of item he has a pointer to.
 */
class Item : public WithPosition
{
protected:
    /*!
     * Default constructor.
     */
    Item();

public:
    ~Item() override;

    /*!
     * Returns type of the item.
     */
    virtual ItemType type() const = 0;

    /*!
     * Clone this item.
     *
     * \a doc Parent of new item.
     */
    virtual QSharedPointer<Item> clone(Document *doc = nullptr) const = 0;

private:
    Q_DISABLE_COPY(Item)
}; // class Item

//
// TextOption
//

/*!
 * \enum MD::TextOption
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Text option.
 *
 * \value TextWithoutFormat No format.
 * \value BoldText Bold text.
 * \value ItalicText Italic text.
 * \value StrikethroughText Strikethrough.
 */
enum TextOption {
    TextWithoutFormat = 0,
    BoldText = 1,
    ItalicText = 2,
    StrikethroughText = 4
}; // enum TextOption

//
// StyleDelim
//

/*!
 * \class MD::StyleDelim
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Emphasis in the Markdown document.
 *
 * Information about where empasises start and end is needed for syntax highlighters.
 * Any item in MD::Paragraph is MD::ItemWithOpts that stores information about
 * openers and closer of emphasises if they are presented before/after the given item
 * in paragraph. A developer may just check MD::ItemWithOpts::openStyles and
 * MD::ItemWithOpts::closeStyles for data in them, and he will know all information
 * about delimiters that started or ended style of the item.
 *
 * Such information may be needed for developing additional styles (plugins), superscripts, subscripts,
 * for example.
 *
 * \sa MD::ItemWithOpts
 */
class StyleDelim final : public WithPosition
{
public:
    /*!
     * Initializing constructor.
     *
     * \a s Style. Actualy it's an ORed combination of MD::TextOption.
     *
     * \a startColumn Start column.
     *
     * \a startLine Start line.
     *
     * \a endColumn End column.
     *
     * \a endLine End line.
     */
    StyleDelim(int s,
               qsizetype startColumn,
               qsizetype startLine,
               qsizetype endColumn,
               qsizetype endLine);

    ~StyleDelim() override;

    /*!
     * Returns style.
     */
    int style() const;

    /*!
     * Set style.
     *
     * \a t New value.
     */
    void setStyle(int t);

private:
    int m_style = TextWithoutFormat;
}; // class StyleDelim

/*!
 * \relates MD::StyleDelim
 * \inheaderfile md4qt/doc.h
 *
 * \brief Returns whether both are equal.
 *
 * \a l Left operand.
 *
 * \a r Right operand.
 */
bool operator==(const StyleDelim &l,
                const StyleDelim &r);

//
// ItemWithOpts
//

/*!
 * \class MD::ItemWithOpts
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Base class for items with style options.
 *
 * Base class for items that can have style options.
 * These are all items in Paragraph.
 */
class ItemWithOpts : public Item
{
protected:
    /*!
     * Default constructor.
     */
    ItemWithOpts();

public:
    ~ItemWithOpts() override;

    /*!
     * Apply other item with options to this.
     *
     * \a other Value to apply.
     */
    void applyItemWithOpts(const ItemWithOpts &other);

    /*!
     * \typealias MD::ItemWithOpts::Styles
     *
     * Type of list of emphasis.
     */
    using Styles = QVector<StyleDelim>;

    /*!
     * Returns style options.
     */
    int opts() const;

    /*!
     * Set style options.
     *
     * \a o New value.
     */
    void setOpts(int o);

    /*!
     * Returns list of all opening emphasises.
     */
    const Styles &openStyles() const;

    /*!
     * Returns list of all opening emphasises.
     */
    Styles &openStyles();

    /*!
     * Returns list of all closing emphasises.
     */
    const Styles &closeStyles() const;

    /*!
     * Returns list of all closing emphasises.
     */
    Styles &closeStyles();

private:
    /*!
     * Style options.
     */
    int m_opts = 0;
    /*!
     * List of opening emphasises.
     */
    Styles m_openStyles;
    /*!
     * List of closing emphasises.
     */
    Styles m_closeStyles;

    Q_DISABLE_COPY(ItemWithOpts)
}; // class ItemWithOpts

//
// PageBreak
//

/*!
 * \class MD::PageBreak
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Page break.
 *
 * Like MD::Anchor starts a file in MD::Document, MD::PageBreak ends a file.
 * MD::PageBreak may appear in MD::Document only in recursive mode.
 *
 * \note The last file in the document does not have a page break.
 */
class PageBreak final : public Item
{
public:
    /*!
     * Default constructor.
     */
    PageBreak();
    ~PageBreak() override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Clone this page break.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

private:
    Q_DISABLE_COPY(PageBreak)
}; // class PageBreak

//
// HorizontalLine
//

/*!
 * \class MD::HorizontalLine
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Horizontal line.
 *
 * Thematic break in Markdown file. In HTML this is usually a \c {<hr />} tag.
 */
class HorizontalLine final : public Item
{
public:
    /*!
     * Default constructor.
     */
    HorizontalLine();
    ~HorizontalLine() override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Clone this horizontal line.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

private:
    Q_DISABLE_COPY(HorizontalLine)
}; // class HorizontalLine

//
// Anchor
//

/*!
 * \class MD::Anchor
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Just an anchor.
 *
 * This library supports recursive Markdown parsing - parsed Markdown file may has links
 * to other Markdown files, that may be parsed recursively with the root file.
 * So in the resulting document can be represented more than one Markdown file. Each file in the
 * document starts with MD::Anchor, it just shows that during traversing through
 * the document you reached new file.
 */
class Anchor final : public Item
{
public:
    /*!
     * Initializing constructor.
     *
     * \a l Label.
     */
    explicit Anchor(const QString &l);
    ~Anchor() override;

    /*!
     * Clone this anchor.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns item type.
     */
    ItemType type() const override;

    /*!
     * Returns label of this anchor.
     */
    const QString &label() const;

private:
    Q_DISABLE_COPY(Anchor)

    /*!
     * Label
     */
    QString m_label;
}; // class Anchor

//
// RawHtml
//

/*!
 * \class MD::RawHtml
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Raw HTML.
 *
 * HTML injection in Markdown. When converting to HTML this item should be converted to HTML
 * by inserting returned by MD::RawHtml::text string.
 */
class RawHtml final : public ItemWithOpts
{
public:
    /*!
     * Default constructor.
     */
    RawHtml();
    ~RawHtml() override;

    /*!
     * Clone this raw HTML.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Returns HTML content.
     */
    const QString &text() const;

    /*!
     * Set HTML content.
     *
     * \a t New value.
     */
    void setText(const QString &t);

private:
    /*!
     * HTML content.
     */
    QString m_text;

    Q_DISABLE_COPY(RawHtml)
}; // class RawHtml

//
// Text
//

/*!
 * \class MD::Text
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Text item in Paragraph.
 *
 * Regular text in paragraph. Information in these items in paragraph is a subject
 * for custom plugins implementation.
 */
class Text : public ItemWithOpts
{
public:
    /*!
     * Default constructor.
     */
    Text();
    ~Text() override;

    /*!
     * Apply other text to this.
     *
     * \a t Value to apply.
     */
    void applyText(const Text &t);

    /*!
     * Clone this text item.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Returns text content.
     */
    const QString &text() const;

    /*!
     * Set text content.
     *
     * \a t New value.
     */
    void setText(const QString &t);

private:
    /*!
     * Text content.
     */
    QString m_text;

    Q_DISABLE_COPY(Text)
}; // class Text

//
// LineBreak
//

/*!
 * \class MD::LineBreak
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Line break.
 *
 * Line break in Markdown. This item in HTML is a usual \c {<br />}.
 */
class LineBreak final : public Text
{
public:
    /*!
     * Default constructor.
     */
    LineBreak();
    ~LineBreak() override;

    /*!
     * Clone this line break.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

private:
    Q_DISABLE_COPY(LineBreak)
}; // class LineBreak

//
// Block
//

/*!
 * \class MD::Block
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Abstract block (storage of child items).
 *
 * Base class for any item in MD::Document that may has children items.
 *
 * \sa MD::Paragraph, MD::Blockquote, MD::List, MD::ListItem, MD::TableCell, MD::Footnote.
 */
class Block : public Item
{
protected:
    /*!
     * Default constructor.
     */
    Block();

public:
    ~Block() override;

    /*!
     * \typealias MD::Block::ItemSharedPointer
     *
     * Type of pointer to child item.
     */
    using ItemSharedPointer = QSharedPointer<Item>;
    /*!
     * \typealias MD::Block::Items
     *
     * Type of list of children.
     */
    using Items = QVector<ItemSharedPointer>;

    /*!
     * Apply other block to this.
     *
     * \a other Value to apply.
     *
     * \a doc Parent of new item.
     */
    void applyBlock(const Block &other,
                    Document *doc = nullptr);

    /*!
     * Returns list of child items.
     */
    const Items &items() const;

    /*!
     * Insert child item at given position.
     *
     * \a idx Index where insert.
     *
     * \a i Item to insert.
     */
    void insertItem(qsizetype idx,
                    ItemSharedPointer i);

    /*!
     * Append child item.
     *
     * \a i Item to append.
     */
    void appendItem(ItemSharedPointer i);

    /*!
     * Remove child item at the given position.
     *
     * \a idx Index where to remove.
     */
    void removeItemAt(qsizetype idx);

    /*!
     * Returns child item at the given position.
     *
     * \a idx Index.
     */
    ItemSharedPointer getItemAt(qsizetype idx) const;

    /*!
     * Returns whether there are no children.
     */
    bool isEmpty() const;

private:
    /*!
     * Child items.
     */
    Items m_items;

    Q_DISABLE_COPY(Block)
}; // class Block

//
// Paragraph
//

/*!
 * \class MD::Paragraph
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Paragraph.
 *
 * Block of inline items, such as MD::Text, MD::Link, MD::Image,
 * MD::RawHtml, MD::Code, MD::Math, MD::LineBreak, MD::FootnoteRef.
 */
class Paragraph final : public Block
{
public:
    /*!
     * Default constructor.
     */
    Paragraph();
    ~Paragraph() override;

    /*!
     * Clone this paragraph.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

private:
    Q_DISABLE_COPY(Paragraph)
}; // class Paragraph

//
// Heading
//

/*!
 * \class MD::Heading
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Heading.
 *
 * Any heading in Markdown.
 */
class Heading final : public Item
{
public:
    /*!
     * Default constructor.
     */
    Heading();
    ~Heading() override;

    /*!
     * \typealias MD::Heading::Delims
     *
     * Type of list of service chanracters.
     */
    using Delims = QVector<WithPosition>;

    /*!
     * Clone this heading.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * \typealias MD::Heading::ParagraphSharedPointer
     *
     * Type of smart pointer to paragraph.
     */
    using ParagraphSharedPointer = QSharedPointer<Paragraph>;

    /*!
     * Returns content of the heading.
     */
    ParagraphSharedPointer text() const;

    /*!
     * Set content of the heading.
     *
     * \a t New value.
     */
    void setText(ParagraphSharedPointer t);

    /*!
     * Returns level of the heading.
     */
    int level() const;

    /*!
     * Set level of the heading.
     *
     * \a l New value.
     */
    void setLevel(int l);

    /*!
     * Returns whether this heading has label?
     */
    bool isLabeled() const;

    /*!
     * Returns label of the heading.
     */
    const QString &label() const;

    /*!
     * Set label of the heading.
     *
     * \a l New value.
     */
    void setLabel(const QString &l);

    /*!
     * Returns list of service characters.
     */
    const Delims &delims() const;

    /*!
     * Set list of service characters.
     *
     * \a d New value.
     */
    void setDelims(const Delims &d);

    /*!
     * Returns position of a label in the heading.
     */
    const WithPosition &labelPos() const;

    /*!
     * Set position of a label in the heading.
     *
     * \a p New value.
     */
    void setLabelPos(const WithPosition &p);

    /*!
     * \typealias MD::Heading::LabelsVector
     *
     * Type of a vector of labels.
     */
    using LabelsVector = QStringList;

    /*!
     * Returns label variants.
     */
    const LabelsVector &labelVariants() const;

    /*!
     * Returns label variants.
     */
    LabelsVector &labelVariants();

    /*!
     * Set label variants.
     *
     * \a vars New value.
     */
    void setLabelVariants(const LabelsVector &vars);

private:
    /*!
     * Content of the heading.
     */
    ParagraphSharedPointer m_text;
    /*!
     * Level of the heading.
     */
    int m_level = 0;
    /*!
     * Label of the heading.
     */
    QString m_label;
    /*!
     * List of service characters.
     */
    Delims m_delims;
    /*!
     * Position of the label.
     */
    WithPosition m_labelPos;
    /*!
     * Label variants.
     */
    LabelsVector m_labelVariants;

    Q_DISABLE_COPY(Heading)
}; // class Heading

//
// Blockquote
//

/*!
 * \class MD::Blockquote
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Blockquote.
 *
 * Blockquote block in Markdown.
 */
class Blockquote final : public Block
{
public:
    /*!
     * Default constructor.
     */
    Blockquote();
    ~Blockquote() override;

    /*!
     * Clone this blockquote.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * \typealias MD::Blockquote::Delims
     *
     * Type of a list of service characters.
     */
    using Delims = QVector<WithPosition>;

    /*!
     * Returns list of service characters.
     */
    const Delims &delims() const;

    /*!
     * Returns list of service characters.
     */
    Delims &delims();

private:
    /*!
     * List of service characters.
     */
    Delims m_delims;

    Q_DISABLE_COPY(Blockquote)
}; // class Blockquote

//
// ListItem
//

/*!
 * \class MD::ListItem
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief List item in a list.
 *
 * List item in a list. List may has only list items, all other content is stored in list items.
 *
 * \sa MD::List
 */
class ListItem final : public Block
{
public:
    /*!
     * Default constructor.
     */
    ListItem();
    ~ListItem() override;

    /*!
     * Clone this list item.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * \enum MD::ListItem::ListType
     *
     * Type of the list.
     *
     * \value Ordered Ordered.
     * \value Unordered Unordered.
     */
    enum ListType {
        Ordered,
        Unordered
    }; // enum ListType

    /*!
     * \enum MD::ListItem::OrderedListPreState
     *
     * Preliminary state of the ordered list.
     *
     * \value Start Start item.
     * \value Continue Continue of the list.
     */
    enum OrderedListPreState {
        Start,
        Continue
    }; // enum OrderedListPreState

    /*!
     * Returns type of the list.
     */
    ListType listType() const;

    /*!
     * Set type of the list.
     *
     * \a t New value.
     */
    void setListType(ListType t);

    /*!
     * Returns preliminary state of the ordered list.
     */
    OrderedListPreState orderedListPreState() const;

    /*!
     * Set preliminary state of the ordered list.
     *
     * \a s New value.
     */
    void setOrderedListPreState(OrderedListPreState s);

    /*!
     * Returns start number of the ordered list.
     */
    int startNumber() const;

    /*!
     * Set start number of the ordered list.
     *
     * \a n New value.
     */
    void setStartNumber(int n);

    /*!
     * Returns whether this list item a task list item?
     */
    bool isTaskList() const;

    /*!
     * Set this list item to be a tsk list item.
     *
     * \a on New value.
     */
    void setTaskList(bool on = true);

    /*!
     * Returns whether this task list item checked?
     */
    bool isChecked() const;

    /*!
     * Set this task list item to be checked.
     *
     * \a on New value.
     */
    void setChecked(bool on = true);

    /*!
     * Returns service character position.
     */
    const WithPosition &delim() const;

    /*!
     * Set service character position.
     *
     * \a d New value.
     */
    void setDelim(const WithPosition &d);

    /*!
     * Returns position of the task list "checkbox" in Markdown.
     */
    const WithPosition &taskDelim() const;

    /*!
     * Set position of the task list "checkbox" in Markdown.
     *
     * \a d New value.
     */
    void setTaskDelim(const WithPosition &d);

private:
    /*!
     * Type of the list.
     */
    ListType m_listType = Unordered;
    /*!
     * Preliminary state of the ordered list.
     */
    OrderedListPreState m_orderedListState = Start;
    /*!
     * Start number of the ordered list.
     */
    int m_startNumber = 1;
    /*!
     * Is this list item a task list item?
     */
    bool m_isTaskList = false;
    /*!
     * Is this task list item checked?
     */
    bool m_isChecked = false;
    /*!
     * Service character position.
     */
    WithPosition m_delim = {};
    /*!
     * Task list "checkbox" position.
     */
    WithPosition m_taskDelim = {};

    Q_DISABLE_COPY(ListItem)
}; // class ListItem

//
// List
//

/*!
 * \class MD::List
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief List.
 *
 * List in Markdown. This class is derived from MD::Block, but actually will has only
 * MD::ListItem as children.
 */
class List final : public Block
{
public:
    /*!
     * Default constructor.
     */
    List();
    ~List() override;

    /*!
     * Clone this list.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

private:
    Q_DISABLE_COPY(List)
}; // class List

//
// LinkBase
//

/*!
 * \class MD::LinkBase
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Base class for links.
 *
 * Base class for any links in Markdown, even images.
 *
 * \sa MD::Link, MD::Image.
 */
class LinkBase : public ItemWithOpts
{
public:
    /*!
     * Default constructor.
     */
    LinkBase();
    ~LinkBase() override;

    /*!
     * Apply other base of link to this.
     *
     * \a other Value to apply.
     *
     * \a doc Parent of new item.
     */
    void applyLinkBase(const LinkBase &other,
                       Document *doc = nullptr);

    /*!
     * \typealias MD::LinkBase::ParagraphSharedPointer
     *
     * Type of a smart pointer to link's description.
     */
    using ParagraphSharedPointer = QSharedPointer<Paragraph>;

    /*!
     * Returns URL of the link.
     */
    const QString &url() const;

    /*!
     * Set URL of the link.
     *
     * \a u New value.
     */
    void setUrl(const QString &u);

    /*!
     * Returns title of the link.
     */
    const QString &title() const;

    /*!
     * Set title of the link.
     *
     * \a t New value.
     */
    void setTitle(const QString &t);

    /*!
     * Returns not parsed text of link's description.
     */
    const QString &text() const;

    /*!
     * Set not parsed text of link's description.
     *
     * \a t New value.
     */
    void setText(const QString &t);

    /*!
     * Returns whether this link empty?
     */
    bool isEmpty() const;

    /*!
     * Returns pointer to parsed text of link's description.
     */
    ParagraphSharedPointer p() const;

    /*!
     * Set pointer to parsed text of link's description.
     *
     * \a v New value.
     */
    void setP(ParagraphSharedPointer v);

    /*!
     * Returns position of link's desciption.
     */
    const WithPosition &textPos() const;

    /*!
     * Set position of link's description.
     *
     * \a pos New value.
     */
    void setTextPos(const WithPosition &pos);

    /*!
     * Returns position of URL.
     */
    const WithPosition &urlPos() const;

    /*!
     * Set position of URL.
     *
     * \a pos New value.
     */
    void setUrlPos(const WithPosition &pos);

private:
    /*!
     * URL.
     */
    QString m_url;
    QString m_title;
    /*!
     * Not parsed content of link's description.
     */
    QString m_text;
    /*!
     * Parsed content of link's description.
     */
    ParagraphSharedPointer m_p;
    /*!
     * Position of link's description.
     */
    WithPosition m_textPos = {};
    /*!
     * URL position.
     */
    WithPosition m_urlPos = {};

    Q_DISABLE_COPY(LinkBase)
}; // class LinkBase

//
// Image
//

/*!
 * \class MD::Image
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Image.
 *
 * Image in Markdown.
 */
class Image final : public LinkBase
{
public:
    /*!
     * Default constructor.
     */
    Image();
    ~Image() override;

    /*!
     * Clone this image.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

private:
    Q_DISABLE_COPY(Image)
}; // class Image

//
// Link
//

/*!
 * \class MD::Link
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Link.
 *
 * Link in Markdown. Doesn't matter what kind of link it is - autolink, shortcut link,
 * GitHub flavored Markdown autolinks, all links will be stored in the document with this item.
 */
class Link final : public LinkBase
{
public:
    /*!
     * Default constructor.
     */
    Link();

    /*!
     * Clone this link.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    ~Link() override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * \typealias MD::Link::ImageSharedPointer
     *
     * Type of a smart pointer to image.
     */
    using ImageSharedPointer = QSharedPointer<Image>;

    /*!
     * Returns image of the link.
     */
    ImageSharedPointer img() const;

    /*!
     * Set image of the link.
     *
     * \a i New value.
     */
    void setImg(ImageSharedPointer i);

private:
    /*!
     * Image of the link.
     */
    ImageSharedPointer m_img;

    Q_DISABLE_COPY(Link)
}; // class Link

//
// Code
//

/*!
 * \class MD::Code
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Code.
 *
 * Block of code in Markdown. Used not only for fenced code blocks, indented code blocks, but
 * for code spans too.
 */
class Code : public ItemWithOpts
{
public:
    /*!
     * Initializing constructor.
     *
     * \a t Code's text.
     *
     * \a fensedCode Whether this code is a fensed code block.
     *
     * \a inl Whether this code is an inline code.
     */
    Code(const QString &t,
         bool fensedCode,
         bool inl);
    ~Code() override;

    /*!
     * Apply other code to this.
     *
     * \a other Value to apply.
     */
    void applyCode(const Code &other);

    /*!
     * Clone this code.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Returns content of the code.
     */
    const QString &text() const;

    /*!
     * Set content of the code.
     *
     * \a t New value.
     */
    void setText(const QString &t);

    /*!
     * Returns whether this code inline?
     */
    bool isInline() const;

    /*!
     * Set this code to be inline.
     *
     * \a on New value.
     */
    void setInline(bool on = true);

    /*!
     * Returns syntax of the fensed code block.
     */
    const QString &syntax() const;

    /*!
     * Set syntax of the fensed code block.
     *
     * \a s New value.
     */
    void setSyntax(const QString &s);

    /*!
     * Returns position of the syntax of the fensed code block.
     */
    const WithPosition &syntaxPos() const;

    /*!
     * Set position of the syntax of the fensed code block.
     *
     * \a p New value.
     */
    void setSyntaxPos(const WithPosition &p);

    /*!
     * Returns position of the start service characters.
     */
    const WithPosition &startDelim() const;

    /*!
     * Set position of the start service characters.
     *
     * \a d New value.
     */
    void setStartDelim(const WithPosition &d);

    /*!
     * Returns position of the ending service characters.
     */
    const WithPosition &endDelim() const;

    /*!
     * Set position of the ending service characters.
     *
     * \a d New value.
     */
    void setEndDelim(const WithPosition &d);

    /*!
     * Returns whether this a fensed code block?
     */
    bool isFensedCode() const;

    /*!
     * Set this code block to be a fensed code block.
     *
     * \a on New value.
     */
    void setFensedCode(bool on = true);

private:
    /*!
     * Content of the code.
     */
    QString m_text;
    /*!
     * Is this code inline?
     */
    bool m_inlined = true;
    /*!
     * Is this code a fensed code block.
     */
    bool m_fensed = false;
    /*!
     * Syntax of the fensed code lock.
     */
    QString m_syntax;
    /*!
     * Position of start service characters.
     */
    WithPosition m_startDelim = {};
    /*!
     * Position of ending service characters.
     */
    WithPosition m_endDelim = {};
    /*!
     * Position of syntax of fensed code block.
     */
    WithPosition m_syntaxPos = {};

    Q_DISABLE_COPY(Code)
}; // class Code

//
// Math
//

/*!
 * \class MD::Math
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief LaTeX math expression.
 *
 * LaTeX math expression in Markdown.
 */
class Math final : public Code
{
public:
    /*!
     * Default constructor.
     */
    Math();
    ~Math() override;

    /*!
     * Clone this LaTeX math expression.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Returns content.
     */
    const QString &expr() const;

    /*!
     * Set content.
     *
     * \a e New value.
     */
    void setExpr(const QString &e);

private:
    Q_DISABLE_COPY(Math)
}; // class Math

//
// TableCell
//

/*!
 * \class MD::TableCell
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Table cell.
 *
 * Block of items in table cell in GitHub flavored Markdown tables.
 *
 * \sa MD::TableRow, MD::Table.
 */
class TableCell final : public Block
{
public:
    /*!
     * Default constructor.
     */
    TableCell();
    ~TableCell() override;

    /*!
     * Clone this table cell.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

private:
    Q_DISABLE_COPY(TableCell)
}; // class TableCell

//
// TableRow
//

/*!
 * \class MD::TableRow
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Table row.
 *
 * Row of table cells in GitHub flavored Markdown tables.
 *
 * \sa MD::TableCell, MD::Table.
 */
class TableRow final : public Item
{
public:
    /*!
     * Default constructor.
     */
    TableRow();
    ~TableRow() override;

    /*!
     * Clone this table row.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * \typealias MD::TableRow::TableCellSharedPointer
     *
     * Type of a smart pointer to table cell.
     */
    using TableCellSharedPointer = QSharedPointer<TableCell>;
    /*!
     * \typealias MD::TableRow::Cells
     *
     * Type of a list of table cells.
     */
    using Cells = QVector<TableCellSharedPointer>;

    /*!
     * Returns list of cells.
     */
    const Cells &cells() const;

    /*!
     * Append cell.
     *
     * \a c New value.
     */
    void appendCell(TableCellSharedPointer c);

    /*!
     * Returns whether this row empty?
     */
    bool isEmpty() const;

private:
    /*!
     * List of cells.
     */
    Cells m_cells;

    Q_DISABLE_COPY(TableRow)
}; // class TableRow

//
// Table
//

/*!
 * \class MD::Table
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Table.
 *
 * GitHub flavored Markdown table.
 *
 * \sa MD::TableCell, MD::TableRow.
 */
class Table final : public Item
{
public:
    /*!
     * Default constructor.
     */
    Table();
    ~Table() override;

    /*!
     * Clone this table.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * \typealias MD::Table::TableRowSharedPointer
     *
     * Type of a smart pointer to table row.
     */
    using TableRowSharedPointer = QSharedPointer<TableRow>;
    /*!
     * \typealias MD::Table::Rows
     *
     * Type of list of rows.
     */
    using Rows = QVector<TableRowSharedPointer>;

    /*!
     * Returns list of rows.
     */
    const Rows &rows() const;

    /*!
     * Append row.
     *
     * \a r New value.
     */
    void appendRow(TableRowSharedPointer r);

    /*!
     * \enum MD::Table::Alignment
     * \inmodule md4qt
     *
     * Alignment.
     *
     * \value AlignLeft Left.
     * \value AlignRight Right.
     * \value AlignCenter Center.
     */
    enum Alignment {
        AlignLeft,
        AlignRight,
        AlignCenter
    }; // enum Alignmnet.

    /*!
     * \typealias MD::Table::ColumnsAlignments
     *
     * Type of list alignments.
     */
    using ColumnsAlignments = QVector<Alignment>;

    /*!
     * Returns alignment of the given column.
     *
     * \a idx Index.
     */
    Alignment columnAlignment(int idx) const;

    /*!
     * Set alignment of the given column.
     *
     * \a idx Index.
     *
     * \a a New value.
     */
    void setColumnAlignment(int idx,
                            Alignment a);

    /*!
     * Returns count of columns.
     */
    int columnsCount() const;

    /*!
     * Returns whether this table empty?
     */
    bool isEmpty() const;

private:
    /*!
     * Rows.
     */
    Rows m_rows;
    /*!
     * Columns' alignments.
     */
    ColumnsAlignments m_aligns;

    Q_DISABLE_COPY(Table)
}; // class Table

//
// FootnoteRef
//

/*!
 * \class MD::FootnoteRef
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Footnote reference.
 *
 * Inline footnote reference in MD::Paragraph. Ususally renders as
 * superscript number.
 */
class FootnoteRef final : public Text
{
public:
    /*!
     * Initializing constructor.
     *
     * \a i ID.
     */
    explicit FootnoteRef(const QString &i);
    ~FootnoteRef() override;

    /*!
     * Clone this footnote reference.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Returns ID of footnote reference.
     */
    const QString &id() const;

    /*!
     * Returns position of ID.
     */
    const WithPosition &idPos() const;

    /*!
     * Set position of ID.
     *
     * \a pos Position.
     */
    void setIdPos(const WithPosition &pos);

private:
    /*!
     * ID.
     */
    QString m_id;
    /*!
     * Position of ID.
     */
    WithPosition m_idPos;

    Q_DISABLE_COPY(FootnoteRef)
}; // class FootnoteRef

//
// Footnote
//

/*!
 * \class MD::Footnote
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Footnote.
 *
 * Footnote block in Markdown.
 */
class Footnote final : public Block
{
public:
    /*!
     * Default constructor.
     */
    Footnote();
    ~Footnote() override;

    /*!
     * Clone this footnote.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Returns position of ID.
     */
    const WithPosition &idPos() const;

    /*!
     * Set position of ID.
     *
     * \a pos Position.
     */
    void setIdPos(const WithPosition &pos);

private:
    /*!
     * Position of ID.
     */
    WithPosition m_idPos = {};

    Q_DISABLE_COPY(Footnote)
}; // class Footnote

//
// Document
//

/*!
 * \class MD::Document
 * \inmodule md4qt
 * \inheaderfile md4qt/doc.h
 *
 * \brief Document.
 *
 * Markdown document.
 */
class Document final : public Block
{
public:
    /*!
     * Default constructor.
     */
    Document();
    ~Document() override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Clone this document.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * \typealias MD::Document::FootnoteSharedPointer
     *
     * Type of a smart pointer to footnote.
     */
    using FootnoteSharedPointer = QSharedPointer<Footnote>;
    /*!
     * \typealias MD::Document::Footnotes
     *
     * Type of a map of footnotes.
     */
    using Footnotes = QMap<QString, FootnoteSharedPointer>;

    /*!
     * Returns map of footnotes.
     */
    const Footnotes &footnotesMap() const;

    /*!
     * Insert footnote with the give ID.
     *
     * \a id ID.
     *
     * \a fn Footnote.
     */
    void insertFootnote(const QString &id,
                        FootnoteSharedPointer fn);

    /*!
     * \typealias MD::Document::LinkSharedPointer
     *
     * Type of a smart pointer to link.
     */
    using LinkSharedPointer = QSharedPointer<Link>;
    /*!
     * \typealias MD::Document::LabeledLinks
     *
     * Type of a map of shortcut links.
     */
    using LabeledLinks = QMap<QString, LinkSharedPointer>;

    /*!
     * Returns map of shortcut links.
     */
    const LabeledLinks &labeledLinks() const;

    /*!
     * Insert shortcut link with the given label.
     *
     * \a label Label.
     *
     * \a lnk Link.
     */
    void insertLabeledLink(const QString &label,
                           LinkSharedPointer lnk);

    /*!
     * \typealias MD::Document::HeadingSharedPointer
     *
     * Type of a smart pointer to heading.
     */
    using HeadingSharedPointer = QSharedPointer<Heading>;
    /*!
     * \typealias MD::Document::LabeledHeadings
     *
     * Type of a map of headings.
     */
    using LabeledHeadings = QMap<QString, HeadingSharedPointer>;

    /*!
     * Returns map of headings.
     */
    const LabeledHeadings &labeledHeadings() const;

    /*!
     * Insert heading with the given label.
     *
     * \a label Label.
     *
     * \a h Heading.
     */
    void insertLabeledHeading(const QString &label,
                              HeadingSharedPointer h);

    /*!
     * \typealias MD::Document::AuxLabelsMap
     *
     * Type of an auxiliary map of labels for headings. Usually it's not needed for user
     * but serves for resolving of conflict of the headings with the same content.
     */
    using AuxLabelsMap = QMap<QString, QMap<QString, qsizetype>>;

    /*!
     * Returns auxiliary map for resolving headings' ids labels.
     */
    AuxLabelsMap &auxLabelsMap();

    /*!
     * Returns auxiliary map for resolving headings' ids labels.
     */
    const AuxLabelsMap &auxLabelsMap() const;

private:
    /*!
     * Map of footnotes.
     */
    Footnotes m_footnotes;
    /*!
     * Map of shortcut links.
     */
    LabeledLinks m_labeledLinks;
    /*!
     * Map of headings.
     */
    LabeledHeadings m_labeledHeadings;
    /*!
     * Auxiliary map for resolving headings' ids labels.
     */
    AuxLabelsMap m_auxLabelsMap;

    Q_DISABLE_COPY(Document)
}; // class Document;

} /* namespace MD */

#endif // MD4QT_MD_DOC_H_INCLUDED
