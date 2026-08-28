/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_VISITOR_H_INCLUDED
#define MD4QT_MD_VISITOR_H_INCLUDED

// md4qt include.
#include "doc.h"

namespace MD
{

//
// Visitor
//

/*!
 * \class MD::Visitor
 * \inmodule md4qt
 * \inheaderfile md4qt/visitor.h
 *
 * \brief Visitor interface to walk through the MD::Document.
 *
 * This is a very basic implementation of walking through the document.
 *
 * \sa MD::details::AlgoVisitor, MD::details::HtmlVisitor, MD::PosCache.
 */
class Visitor
{
public:
    /*!
     * Default constructor.
     */
    Visitor();
    virtual ~Visitor();

    /*!
     * Walk through the document.
     *
     * \a d Markdown document.
     */
    void process(QSharedPointer<Document> d);

protected:
    /*!
     * For some generator it's important to keep line endings like they were in Markdown.
     * So onParagraph() method invokes this method when necessary to add line ending.
     */
    virtual void onAddLineEnding() = 0;

    /*!
     * Handle user-defined item.
     *
     * \a item Item.
     */
    virtual void onUserDefined(Item *item);

    /*!
     * Handle text item.
     *
     * \a t Text.
     */
    virtual void onText(Text *t) = 0;

    /*!
     * Handle LaTeX math expression.
     *
     * \a m Math.
     */
    virtual void onMath(Math *m) = 0;

    /*!
     * Handle line break.
     *
     * \a b Line break.
     */
    virtual void onLineBreak(LineBreak *b) = 0;

    /*!
     * Handle paragraph.
     *
     * \a p Paragraph.
     *
     * \a wrap Wrap this paragraph with something or no? It's useful to not wrap standalone
     *         paragraph in list item, for example.
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    virtual void onParagraph(Paragraph *p,
                             bool wrap,
                             bool skipOpeningWrap = false);

    /*!
     * Handle heading.
     *
     * \a h Heading.
     */
    virtual void onHeading(Heading *h) = 0;

    /*!
     * Handle code.
     *
     * \a c Code.
     */
    virtual void onCode(Code *c) = 0;

    /*!
     * Handle inline code.
     *
     * \a c Code
     */
    virtual void onInlineCode(Code *c) = 0;

    /*!
     * Handle blockquote.
     *
     * \a b Blockquote.
     */
    virtual void onBlockquote(Blockquote *b);

    /*!
     * Handle list.
     *
     * \a l List.
     */
    virtual void onList(List *l) = 0;

    /*!
     * Handle table.
     *
     * \a t Table.
     */
    virtual void onTable(Table *t) = 0;

    /*!
     * Handle anchor.
     *
     * \a a Anchor.
     */
    virtual void onAnchor(Anchor *a) = 0;

    /*!
     * Handle raw HTML.
     *
     * \a h Raw HTML.
     */
    virtual void onRawHtml(RawHtml *h) = 0;

    /*!
     * Handle horizontal line.
     *
     * \a l Horizontal line.
     */
    virtual void onHorizontalLine(HorizontalLine *l) = 0;

    /*!
     * Handle link.
     *
     * \a l Link.
     */
    virtual void onLink(Link *l) = 0;

    /*!
     * Handle image.
     *
     * \a i Image.
     */
    virtual void onImage(Image *i) = 0;

    /*!
     * Handle footnote reference.
     *
     * \a ref Footnote reference.
     */
    virtual void onFootnoteRef(FootnoteRef *ref) = 0;

    /*!
     * Handle list item.
     *
     * \a i List item.
     *
     * \a first Is this item first in the list?
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    virtual void onListItem(ListItem *i,
                            bool first,
                            bool skipOpeningWrap = false);

    /*!
     * Handle table cell.
     *
     * \a c Table cell.
     */
    virtual void onTableCell(TableCell *c);

    /*!
     * Handle footnote.
     *
     * \a f Footnote.
     */
    virtual void onFootnote(Footnote *f);

    /*!
     * Returns whether first paragraph in list item should be wrapped.
     *
     * \a i List item.
     */
    virtual bool wrapFirstParagraphInListItem(ListItem *i) const;

protected:
    /*!
     * All available m_anchors in the document.
     */
    QStringList m_anchors;
    /*!
     * Document.
     */
    QSharedPointer<Document> m_doc;
}; // class Visitor

} /* namespace MD */

#endif // MD4QT_MD_VISITOR_H_INCLUDED
