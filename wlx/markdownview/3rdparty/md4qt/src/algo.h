/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_ALGO_H_INCLUDED
#define MD4QT_MD_ALGO_H_INCLUDED

// md4qt include.
#include "visitor.h"

// C++ include.
#include <functional>

namespace MD
{

/*!
 * \inmodule md4qt
 * \typealias MD::ItemFunctor
 * \inheaderfile md4qt/algo.h
 *
 * \brief Function type for algorithms.
 */
using ItemFunctor = std::function<void(Item *)>;

/*!
 * \namespace MD::details
 * \inmodule md4qt
 * \inheaderfile md4qt/algo.h
 *
 * \brief Namespace for some implemetation details, but useful for reuse.
 *
 * Namespace for reusable auxiliary data structures.
 */
namespace details
{

//
// AlgoVisitor
//

/*!
 * \class MD::details::AlgoVisitor
 * \inmodule md4qt
 * \inheaderfile md4qt/algo.h
 *
 * \brief Visitor for algorithms.
 *
 * The main difference from MD::Visitor is that this class during walking through the
 * document takes into account nesting level of the item. If nesting level reached defined value
 * it won't process anything further. And if type of the item is in set types walking through
 * the document will invoke a given function object for that item.
 *
 * \sa MD::Visitor
 */
class AlgoVisitor : public Visitor
{
public:
    /*!
     * Constructor
     *
     * \a mnl Maximum nesting level
     *
     * \a t List of items' types.
     *
     * \a f Functor.
     */
    AlgoVisitor(unsigned int mnl,
                const QVector<ItemType> &t,
                ItemFunctor f);
    ~AlgoVisitor() override;

    /*!
     * Walk through the document.
     *
     * \a doc Document.
     */
    virtual void walk(QSharedPointer<Document> doc);

protected:
    void onAddLineEnding() override;

    /*!
     * Process user defined item type.
     *
     * \a i Item.
     */
    void onUserDefined(Item *i) override;

    /*!
     * Process text item.
     *
     * \a t Text.
     */
    void onText(Text *t) override;

    /*!
     * Process LaTeX math item.
     *
     * \a m Math.
     */
    void onMath(Math *m) override;

    /*!
     * Process line break.
     *
     * \a l Line break.
     */
    void onLineBreak(LineBreak *l) override;

    /*!
     * Process paragraph.
     *
     * \a p Paragraph.
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    void onParagraph(Paragraph *p,
                     bool,
                     bool skipOpeningWrap = false) override;

    /*!
     * Process heading.
     *
     * \a h Heading.
     */
    void onHeading(Heading *h) override;

    /*!
     * Process code block.
     *
     * \a c Code.
     */
    void onCode(Code *c) override;

    /*!
     * Process inline code.
     *
     * \a c Code.
     */
    void onInlineCode(Code *c) override;

    /*!
     * Process blockquote.
     *
     * \a b Blockquote.
     */
    void onBlockquote(Blockquote *b) override;

    /*!
     * Process list.
     *
     * \a l List.
     */
    void onList(List *l) override;

    /*!
     * Process table.
     *
     * \a t Table.
     */
    void onTable(Table *t) override;

    /*!
     * Process anchor.
     *
     * \a a Anchor.
     */
    void onAnchor(Anchor *a) override;

    /*!
     * Process raw HTML block.
     *
     * \a h Raw HTML.
     */
    void onRawHtml(RawHtml *h) override;

    /*!
     * Process horizontal line.
     *
     * \a l Horizontal line.
     */
    void onHorizontalLine(HorizontalLine *l) override;

    /*!
     * Process link.
     *
     * \a l Link.
     */
    void onLink(Link *l) override;

    /*!
     * Process Image.
     *
     * \a i Image.
     */
    void onImage(Image *i) override;

    /*!
     * Process footnote reference.
     *
     * \a ref Footnote reference.
     */
    void onFootnoteRef(FootnoteRef *ref) override;

    /*!
     * Process list item.
     *
     * \a i List item.
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    void onListItem(ListItem *i,
                    bool,
                    bool skipOpeningWrap = false) override;

    /*!
     * Process table cell.
     *
     * \a c Table cell.
     */
    void onTableCell(TableCell *c) override;

    /*!
     * Process footnote.
     *
     * \a f Footnote.
     */
    void onFootnote(Footnote *f) override;

    /*!
     * Process all footnotes.
     *
     * \a doc Document.
     */
    virtual void onFootnotes(QSharedPointer<Document> doc);

protected:
    /*!
     * Current nesting level, increments/decrements during walking through te document.
     */
    unsigned int m_currentNestingLevel = 0;
    /*!
     * Maximum allowed nesting level.
     */
    unsigned int m_maxNestingLevel = 0;
    /*!
     * List of types that should be processed.
     */
    const QVector<ItemType> &m_types;
    /*!
     * Functor that will be invoked if all circumstances will be observed.
     */
    ItemFunctor m_func = {};

    /*!
     * \inmodule md4qt
     *
     * \brief Auxiliary structure for MD::details::AlgoVisitor.
     *
     * Auxiliary structure to increment/decrement nesting level during walking through
     * the document, and checking circumstances for the algorithm.
     */
    struct IncrementNestingLevel {
        /*!
         * Constructor.
         *
         * \a l Level.
         *
         * \a m Max nesting level.
         *
         * \a t List of types.
         */
        IncrementNestingLevel(unsigned int &l,
                              unsigned int m,
                              const QVector<ItemType> &t);
        ~IncrementNestingLevel();

        /*!
         * Returns whether this item type allowed at this nesting level.
         *
         * \a t Item's type to check.
         */
        bool allowed(ItemType t) const;

        /*!
         * Returns whether next nesting level allowed.
         */
        bool nextAllowed() const;

        /*!
         * Reference to nesting level.
         */
        unsigned int &m_level;
        /*!
         * Maximum allowed nesting level.
         */
        unsigned int m_maxNestingLevel;
        /*!
         * Reference to list of allowed types.
         */
        const QVector<ItemType> &m_types;
    }; // struct IncrementNestingLevel
}; // class AlgoVisitor

} /* namespace details */

/*!
 * \inheaderfile md4qt/algo.h
 *
 * \brief Calls function for each item in the document with the given type.
 *
 * \a types Vector of item's types to be processed.
 *
 * \a doc Document.
 *
 * \a func Functor object.
 *
 * \a maxNestingLevel Maximun nesting level. 0 means infinity, 1 - only top level items...
 */
void forEach(const QVector<ItemType> &types,
             QSharedPointer<Document> doc,
             ItemFunctor func,
             unsigned int maxNestingLevel = 0);

} /* namespace MD */

#endif // MD4QT_MD_ALGO_H_INCLUDED
