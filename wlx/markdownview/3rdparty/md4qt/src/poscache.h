/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_POSCACHE_H_INCLUDED
#define MD4QT_MD_POSCACHE_H_INCLUDED

#include "visitor.h"

// Qt include.
#include <QStack>
#include <QVector>

namespace MD
{

namespace details
{

//
// PosRange
//

/*!
 * \class MD::details::PosRange
 * \inmodule md4qt
 * \inheaderfile md4qt/poscache.h
 *
 * \brief Cached position of MD::Item.
 *
 * Just a data structure to hold information about positions and pointers to items.
 * Purely for internal use, a developer should not have a need to work with this structure.
 */
struct PosRange {
    /*!
     * Initializing constructor.
     *
     * \a startColumn Start column.
     *
     * \a startLine Start line.
     *
     * \a endColumn End column.
     *
     * \a endLine End line.
     */
    PosRange(qsizetype startColumn,
             qsizetype startLine,
             qsizetype endColumn,
             qsizetype endLine);

    /*!
     * Initializing constructor.
     *
     * \a startColumn Start column.
     *
     * \a startLine Start line.
     *
     * \a endColumn End column.
     *
     * \a endLine End line.
     *
     * \a item This item.
     */
    PosRange(qsizetype startColumn,
             qsizetype startLine,
             qsizetype endColumn,
             qsizetype endLine,
             Item *item);

    /*!
     * Initializing constructor.
     *
     * \a startColumn Start column.
     *
     * \a startLine Start line.
     *
     * \a endColumn End column.
     *
     * \a endLine End line.
     *
     * \a item This item.
     *
     * \a children Children items.
     */
    PosRange(qsizetype startColumn,
             qsizetype startLine,
             qsizetype endColumn,
             qsizetype endLine,
             Item *item,
             const QVector<QSharedPointer<PosRange>> &children);

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
    /*!
     * Pointer to this item.
     */
    Item *m_item = nullptr;
    /*!
     * List of children.
     */
    QVector<QSharedPointer<PosRange>> m_children = {};

    /*!
     * Returns whether position valid.
     */
    inline bool isValidPos() const;
};

// Look at this equality operator like on rectangles intersection.
// If rectangles in text intersect then rectangles are equal.
/*!
 * \relates MD::details::PosRange
 * \inheaderfile md4qt/poscache.h
 *
 * Returns whether both are equal.
 *
 * \a l Left operand.
 *
 * \a r Right operand.
 */
bool operator==(const PosRange &l,
                const PosRange &r);

/*!
 * \relates MD::details::PosRange
 * \inheaderfile md4qt/poscache.h
 *
 * Returns whether left is less of right.
 *
 * \a l Left operand.
 *
 * \a r Right operand.
 */
bool operator<(const PosRange &l,
               const PosRange &r);

} /* namespace details */

bool comparePosRangeLower(const QSharedPointer<details::PosRange> &ptr,
                          const details::PosRange &range);

bool comparePosRangeUpper(const details::PosRange &range,
                          const QSharedPointer<details::PosRange> &ptr);

//
// PosCache
//

/*!
 * \class MD::PosCache
 * \inmodule md4qt
 * \inheaderfile md4qt/poscache.h
 *
 * \brief Cache of Markdown items to be accessed via position.
 *
 * A visitor that during walking through a document gathers information about positions of items
 * and stores it internally. When positions cache is initialized a developer can search for items
 * by its positions with MD::PosCache::findFirstInCache method.
 *
 * A complexity of walking is O(N), whereas searching is LOG(N).
 *
 * \sa MD::Visitor
 */
class PosCache : public MD::Visitor
{
public:
    /*!
     * Default constructor.
     */
    PosCache();
    ~PosCache() override;

    /*!
     * Initialize m_cache with the given document.
     *
     * \note Document should not be recursive.
     *
     * \a doc Document.
     */
    virtual void initialize(QSharedPointer<MD::Document> doc);

    /*!
     * \class MD::PosCache::Items
     * \inmodule md4qt
     *
     * \brief Vector of items.
     *
     * Vector with items, where front is a top-level item,
     * and back is most nested child.
     */
    using Items = QVector<Item *>;

    /*!
     * Returns first occurence of Markdown item with all first children by the given position.
     *
     * \a pos Position.
     */
    Items findFirstInCache(const MD::WithPosition &pos) const;

protected:
    /*!
     * Find in cache an item with the given position.
     *
     * \a vec Cache of position.
     *
     * \a pos Position of sought-for item.
     *
     * \a ordered Indicates that we sure that searching item places after everything.
     */
    details::PosRange *findInCache(const QVector<QSharedPointer<details::PosRange>> &vec,
                                   const details::PosRange &pos,
                                   bool ordered = false) const;

    /*!
     * Find in cache items with the given position with all parents.
     *
     * \a vec Cache.
     *
     * \a pos Position of sought-for item.
     *
     * \a res Reference to result of search.
     */
    void findFirstInCache(const QVector<QSharedPointer<details::PosRange>> &vec,
                          const details::PosRange &pos,
                          Items &res) const;

    /*!
     * Insert in cache.
     *
     * \a item Position for insertion.
     *
     * \a sort Should we sord when insert top-level item, or we can be sure that this item is last?
     *
     * \a insertInStack Indicates that this item is some kind a block with children that should be
     *                  added into stack for fast finding parent item for the next children. This
     *                  techinque allows to have O(N) walking complexity.
     */
    void insertInCache(const details::PosRange &item,
                       bool sort = false,
                       bool insertInStack = false);

protected:
    /*!
     * Cache user defined item.
     *
     * \a i Item.
     */
    void onUserDefined(Item *i) override;

    /*!
     * Cache shortcut link.
     *
     * \a l Link.
     */
    virtual void onReferenceLink(Link *l);

    void onAddLineEnding() override;

    /*!
     * Cache text item.
     *
     * \a t Text.
     */
    void onText(Text *t) override;

    /*!
     * Cache LaTeX math expression.
     *
     * \a m Math.
     */
    void onMath(Math *m) override;

    void onLineBreak(LineBreak *) override;

    /*!
     * Cache paragraph.
     *
     * \a p Paragraph.
     *
     * \a wrap Wrap this paragraph with something or no? It's useful to not wrap standalone
     *         paragraph in list item, for example.
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    void onParagraph(Paragraph *p,
                     bool wrap,
                     bool skipOpeningWrap = false) override;

    /*!
     * Cache heading.
     *
     * \a h Heading.
     */
    void onHeading(Heading *h) override;

    /*!
     * Cache code.
     *
     * \a c Code.
     */
    void onCode(Code *c) override;

    /*!
     * Cache inline code.
     *
     * \a c Code.
     */
    void onInlineCode(Code *c) override;

    /*!
     * Cache blockquote.
     *
     * \a b Blockquote.
     */
    void onBlockquote(Blockquote *b) override;

    /*!
     * Cache list.
     *
     * \a l List.
     */
    void onList(List *l) override;

    /*!
     * Cache table.
     *
     * \a t Table.
     */
    void onTable(Table *t) override;

    void onAnchor(Anchor *) override;

    /*!
     * Cache raw HTML.
     *
     * \a h Raw HTML.
     */
    void onRawHtml(RawHtml *h) override;

    /*!
     * Cache horizontal line.
     *
     * \a l Horizontal line.
     */
    void onHorizontalLine(HorizontalLine *l) override;

    /*!
     * Cache link.
     *
     * \a l Link.
     */
    void onLink(Link *l) override;

    /*!
     * Cache image.
     *
     * \a i Image.
     */
    void onImage(Image *i) override;

    /*!
     * Cache footnote reference.
     *
     * \a ref Footnote reference.
     */
    void onFootnoteRef(FootnoteRef *ref) override;

    /*!
     * Cache footnote.
     *
     * \a f Footnote.
     */
    void onFootnote(Footnote *f) override;

    /*!
     * Cache list item.
     *
     * \a l List item.
     *
     * \a first Is this item first in the list?
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    void onListItem(ListItem *l,
                    bool first,
                    bool skipOpeningWrap = false) override;

protected:
    /*!
     * Cache.
     */
    QVector<QSharedPointer<details::PosRange>> m_cache;
    /*!
     * Skip adding in cache.
     */
    bool m_skipInCache = false;

private:
    QStack<QSharedPointer<details::PosRange>> m_currentItems;
}; // class PosCache

} /* namespace MD */

#endif // MD4QT_MD_POSCACHE_H_INCLUDED
