/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_HTML_H_INCLUDED
#define MD4QT_MD_HTML_H_INCLUDED

// md4qt include.
#include "doc.h"
#include "visitor.h"

// Qt include.
#include <QHash>

namespace MD
{

namespace details
{

//
// IdsMap
//

/*!
 * \typealias MD::details::IdsMap
 * \inmodule md4qt
 * \inheaderfile md4qt/html.h
 *
 * \brief Map of IDs to set them for items in HTML.
 *
 * This is a map of IDs that should be set in HTML for corresponding items.
 */
using IdsMap = QHash<MD::Item *, QString>;

//
// HtmlVisitor
//

/*!
 * \class MD::details::HtmlVisitor
 * \inmodule md4qt
 * \inheaderfile md4qt/html.h
 *
 * \brief HTML visitor interface to walk through the MD:Document.
 *
 * This is visitor to convert Markdown document to HTML string. This class is highly
 * customizable, absolutelly everything may be overriden, all data members are protected
 * so a developer won't have problems with customizaion.
 *
 * As example of customization have a look at the following code.
 *
 * \code
 * class HtmlVisitor : public MD::details::HtmlVisitor<MD::QStringTrait>
 * {
 * protected:
 *     QString prepareTextForHtml(const QString &t) override
 *     {
 *         auto tmp = MD::details::HtmlVisitor<MD::QStringTrait>::prepareTextForHtml(t);
 *         tmp.replace(QLatin1Char('$'), QStringLiteral("<span>$</span>"));
 *
 *         return tmp;
 *     }
 * };
 * \endcode
 *
 * In this example we convert \c {$} character to \c {<span>$</span>} when inserting string to HTML.
 * This is needed to disable processing of \c {$} character in HTML as start/end of \c {LaTeX} equation
 * by some JavaScript, like \c {KaTeX}, to prevent wrong rendering of standalone \c {$} character.
 *
 * MD::details::HtmlVisitor doesn't do this protection, so if in your case you use rendering of \c {LaTeX}
 * math equation and enframe them by \c {$} character in HTML, you will need something like the above example.
 */
class HtmlVisitor : public Visitor
{
public:
    /*!
     * Default constructor.
     */
    HtmlVisitor();
    ~HtmlVisitor() override;

    /*!
     * Walk through the document.
     *
     * \a doc Document.
     *
     * \a footnoteBackLinkContent String that will be applied as content of back link from footnote.
     *                            As an example, you can use "<img src="..." />".
     *
     * \a wrappedInArticle Wrap HTML with <article> tag?
     *
     * \a idsMap Map of IDs to set to corresonding items.
     */
    virtual QString toHtml(QSharedPointer<Document> doc,
                           const QString &footnoteBackLinkContent,
                           bool wrappedInArticle = true,
                           const IdsMap *idsMap = nullptr);

protected:
    /*!
     * Returns ID of item if it's set.
     *
     * \a item Item.
     */
    virtual QString getId(Item *item) const;

    /*!
     * Append ID into HTML.
     *
     * \a item Item.
     */
    virtual void printId(Item *item);

    /*!
     * Insert into HTML tags for opening styles.
     *
     * \a styles Styles.
     */
    virtual void openStyle(const typename ItemWithOpts::Styles &styles);

    /*!
     * Insert into HTML tags for closing styles.
     *
     * \a styles Styles.
     */
    virtual void closeStyle(const typename ItemWithOpts::Styles &styles);

    /*!
     * Handle new line in HTML.
     */
    void onAddLineEnding() override;

    /*!
     * Handle text item.
     *
     * \a t Text.
     */
    void onText(Text *t) override;

    /*!
     * Handle LaTeX math expression.
     *
     * \a m Math.
     */
    void onMath(Math *m) override;

    /*!
     * Handle line break.
     */
    void onLineBreak(LineBreak *) override;

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
    void onParagraph(Paragraph *p,
                     bool wrap,
                     bool skipOpeningWrap = false) override;

    /*!
     * Handle heading.
     *
     * \a h Heading.
     */
    void onHeading(Heading *h) override;

    /*!
     * Handle code block.
     *
     * \a c Code.
     */
    void onCode(Code *c) override;

    /*!
     * Handle inline code.
     *
     * \a c Code.
     */
    void onInlineCode(Code *c) override;

    /*!
     * Handle blockquote.
     *
     * \a b Blockquote.
     */
    void onBlockquote(Blockquote *b) override;

    /*!
     * Handle list.
     *
     * \a l List.
     */
    void onList(List *l) override;

    /*!
     * Handle table.
     *
     * \a t Table.
     */
    void onTable(Table *t) override;

    /*!
     * Handle anchor.
     *
     * \a a Anchor.
     */
    void onAnchor(Anchor *a) override;

    /*!
     * Handle raw HTML.
     *
     * \a h Raw HTML.
     */
    void onRawHtml(RawHtml *h) override;

    /*!
     * Handle horizontal line.
     */
    void onHorizontalLine(HorizontalLine *) override;

    /*!
     * Handle link.
     *
     * \a l Link.
     */
    void onLink(Link *l) override;

    /*!
     * Handle image.
     *
     * \a i Image.
     */
    void onImage(Image *i) override;

    /*!
     * Handle footnote reference.
     *
     * \a ref Footnote reference.
     */
    void onFootnoteRef(FootnoteRef *ref) override;

    /*!
     * Handle list item.
     *
     * \a i List item.
     *
     * \a first Is this item first in the list?
     *
     * \a skipOpeningWrap Indicates that opening wrap should be added or no.
     */
    void onListItem(ListItem *i,
                    bool first,
                    bool skipOpeningWrap = false) override;

    /*!
     * Handle heading.
     *
     * \a h Heading.
     *
     * \a ht Heading tag.
     */
    virtual void onHeading(Heading *h,
                           const QString &ht);

    /*!
     * Handle footnotes.
     *
     * \a footnoteBackLinkContent String that will be applied as content of back link from footnote.
     *                            As an example, you can use "<img src="..." />".
     */
    virtual void onFootnotes(const QString &footnoteBackLinkContent);

    /*!
     * Returns HTML content for heading's ID.
     *
     * \a h Heading.
     */
    virtual QString headingIdToHtml(Heading *h);

    /*!
     * Prepare text to insert into HTML content.
     *
     * \a t String.
     */
    virtual QString prepareTextForHtml(const QString &t);

    /*!
     * Returns HTML content for table alignment.
     *
     * \a a Alignment.
     */
    virtual QString tableAlignmentToHtml(typename Table::Alignment a);

protected:
    /*!
     * HTML content.
     */
    QString m_html;
    /*!
     * Just collect footnote references?
     */
    bool m_justCollectFootnoteRefs = false;
    /*!
     * Just process footnote references and don't increment count number.
     */
    bool m_dontIncrementFootnoteCount = false;
    /*!
     * Is this HTML wrapped in artcile tag?
     */
    bool m_isWrappedInArticle = true;

    /*!
     * \class MD::details::HtmlVisitor::FootnoteRefStuff
     * \inmodule md4qt
     *
     * \brief Auxiliary struct to process footnotes.
     *
     * This structure is used to form ID of footnote reference in generated HTML.
     *
     * ID of footnote reference in HTML looks like "ref-{FOOTNOTE_ID}-{NUMBER_OF_REFERENCE_WITH_THIS_FOOTNOTE_ID}"
     */
    struct FootnoteRefStuff {
        /*!
         * ID of footnote.
         *
         * This ID places to {FOOTNOTE_ID}.
         */
        QString m_id;
        /*!
         * Count of references with this ID.
         *
         * Used to generate back links from footnote to footnote reference.
         */
        qsizetype m_count = 0;
        /*!
         * Number of footnote reference with this ID.
         *
         * This number places to {NUMBER_OF_REFERENCE_WITH_THIS_FOOTNOTE_ID}.
         */
        qsizetype m_current = 0;
    };

    /*!
     * Vector of processed footnotes references.
     */
    QVector<FootnoteRefStuff> m_fns;
    /*!
     * Map of IDs to set to corresponding items.
     */
    const IdsMap *m_idsMap = nullptr;
}; // class HtmlVisitor

} /* namespace details */

/*!
 * \inheaderfile md4qt/html.h
 *
 * \brief Convert Document to HTML.
 *
 * \a doc Markdown document.
 *
 * \a wrapInBodyTag Wrap HTML into <body> tag?
 *
 * \a footnoteBackLinkContent String that will be applied as content of back link from footnote.
 *                            As an example, you can use "<img src="..." />".
 *
 * \a wrapInArticle Wrap HTML with <article> tag?
 *
 * \a idsMap Map of IDs to set to items.
 */
template<class HtmlVisitor = details::HtmlVisitor>
inline QString toHtml(QSharedPointer<Document> doc,
                      bool wrapInBodyTag = true,
                      const QString &footnoteBackLinkContent = {},
                      bool wrapInArticle = true,
                      const details::IdsMap *idsMap = nullptr)
{
    QString html;

    if (wrapInBodyTag) {
        html.push_back(QStringLiteral("<!DOCTYPE html>\n<html><head></head><body>\n"));
    }

    if (wrapInArticle) {
        html.push_back(QStringLiteral("<article class=\"markdown-body\">"));
    }

    HtmlVisitor visitor;

    html.push_back(visitor.toHtml(doc, footnoteBackLinkContent, wrapInArticle, idsMap));

    if (wrapInArticle) {
        html.push_back(QStringLiteral("</article>\n"));
    }

    if (wrapInBodyTag) {
        html.push_back(QStringLiteral("</body></html>\n"));
    }

    return html;
}

} /* namespace MD */

#endif // MD4QT_MD_HTML_H_INCLUDED
