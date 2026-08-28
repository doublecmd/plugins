/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "html.h"

namespace MD
{

namespace details
{

//
// HtmlVisitor
//

HtmlVisitor::HtmlVisitor() = default;

HtmlVisitor::~HtmlVisitor() = default;

QString HtmlVisitor::toHtml(QSharedPointer<Document> doc,
                            const QString &footnoteBackLinkContent,
                            bool wrappedInArticle,
                            const IdsMap *idsMap)
{
    m_isWrappedInArticle = wrappedInArticle;
    m_idsMap = idsMap;

    m_html.clear();
    m_fns.clear();

    this->process(doc);

    onFootnotes(footnoteBackLinkContent);

    return m_html;
}

QString HtmlVisitor::getId(Item *item) const
{
    if (item->type() != ItemType::Heading) {
        if (m_idsMap) {
            const auto it = m_idsMap->find(item);

            if (it != m_idsMap->cend()) {
                return *it;
            }
        }
    } else {
        return static_cast<Heading *>(item)->label();
    }

    return {};
}

void HtmlVisitor::printId(Item *item)
{
    const auto id = getId(item);

    if (!id.isEmpty()) {
        m_html.push_back(QStringLiteral(" id=\""));
        m_html.push_back(id);
        m_html.push_back(QStringLiteral("\""));
    }
}

void HtmlVisitor::openStyle(const typename ItemWithOpts::Styles &styles)
{
    for (const auto &s : styles) {
        switch (s.style()) {
        case TextOption::BoldText:
            m_html.push_back(QStringLiteral("<strong>"));
            break;

        case TextOption::ItalicText:
            m_html.push_back(QStringLiteral("<em>"));
            break;

        case TextOption::StrikethroughText:
            m_html.push_back(QStringLiteral("<del>"));
            break;

        // LCOV_EXCL_START
        default:
            break;
            // LCOV_EXCL_STOP
        }
    }
}

void HtmlVisitor::closeStyle(const typename ItemWithOpts::Styles &styles)
{
    for (const auto &s : styles) {
        switch (s.style()) {
        case TextOption::BoldText:
            m_html.push_back(QStringLiteral("</strong>"));
            break;

        case TextOption::ItalicText:
            m_html.push_back(QStringLiteral("</em>"));
            break;

        case TextOption::StrikethroughText:
            m_html.push_back(QStringLiteral("</del>"));
            break;

        // LCOV_EXCL_START
        default:
            break;
            // LCOV_EXCL_STOP
        }
    }
}

void HtmlVisitor::onAddLineEnding()
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
    }
}

void HtmlVisitor::onText(Text *t)
{
    if (!m_justCollectFootnoteRefs) {
        openStyle(t->openStyles());

        m_html.push_back(prepareTextForHtml(t->text()));

        closeStyle(t->closeStyles());
    }
}

void HtmlVisitor::onMath(Math *m)
{
    if (!m_justCollectFootnoteRefs) {
        openStyle(m->openStyles());

        m_html.push_back(m->isInline() ? QStringLiteral("$") : QStringLiteral("$$"));
        m_html.push_back(prepareTextForHtml(m->expr()));
        m_html.push_back(m->isInline() ? QStringLiteral("$") : QStringLiteral("$$"));

        closeStyle(m->closeStyles());
    }
}

void HtmlVisitor::onLineBreak(LineBreak *)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("<br />"));
    }
}

void HtmlVisitor::onParagraph(Paragraph *p,
                              bool wrap,
                              bool skipOpeningWrap)
{
    if (wrap && !m_justCollectFootnoteRefs && !skipOpeningWrap) {
        m_html.push_back(QStringLiteral("<p dir=\"auto\""));
        printId(p);
        m_html.push_back(QStringLiteral(">"));
    }

    Visitor::onParagraph(p, wrap);

    if (wrap && !m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("</p>"));
    }
}

void HtmlVisitor::onHeading(Heading *h)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
    }

    switch (h->level()) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        onHeading(h, QStringLiteral("h") + QString::number(h->level()));
        break;

    // LCOV_EXCL_START
    default:
        break;
        // LCOV_EXCL_STOP
    }

    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
    }
}

void HtmlVisitor::onCode(Code *c)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
        m_html.push_back(QStringLiteral("<pre"));
        printId(c);
        m_html.push_back(QStringLiteral("><code"));

        if (!c->syntax().isEmpty()) {
            m_html.push_back(QStringLiteral(" class=\"language-"));
            m_html.push_back(c->syntax());
            m_html.push_back(QStringLiteral("\""));
        }

        m_html.push_back(QStringLiteral(">"));
        m_html.push_back(prepareTextForHtml(c->text()));
        m_html.push_back(QStringLiteral("</code></pre>"));
        m_html.push_back(QStringLiteral("\n"));
    }
}

void HtmlVisitor::onInlineCode(Code *c)
{
    if (!m_justCollectFootnoteRefs) {
        openStyle(c->openStyles());

        m_html.push_back(QStringLiteral("<code"));
        printId(c);
        m_html.push_back(QStringLiteral(">"));

        m_html.push_back(prepareTextForHtml(c->text()));

        m_html.push_back(QStringLiteral("</code>"));

        closeStyle(c->closeStyles());
    }
}

void HtmlVisitor::onBlockquote(Blockquote *b)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n<blockquote"));
        printId(b);
        m_html.push_back(QStringLiteral(">"));
    }

    Visitor::onBlockquote(b);

    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("</blockquote>\n"));
    }
}

void HtmlVisitor::onList(List *l)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
    }

    typename ListItem::ListType type = ListItem::Ordered;
    bool first = true;

    for (auto it = l->items().cbegin(), last = l->items().cend(); it != last; ++it) {
        if ((*it)->type() == ItemType::ListItem) {
            auto *item = static_cast<ListItem *>(it->get());

            if (first) {
                type = item->listType();

                if (type == ListItem::Ordered) {
                    if (!m_justCollectFootnoteRefs) {
                        m_html.push_back(QStringLiteral("<ol"));
                        printId(l);

                        if (item->isTaskList()) {
                            m_html.push_back(QStringLiteral(" class=\"contains-task-list\""));
                        }

                        m_html.push_back(QStringLiteral(" dir=\"auto\">\n"));
                    }
                } else {
                    if (!m_justCollectFootnoteRefs) {
                        m_html.push_back(QStringLiteral("<ul"));
                        printId(l);

                        if (item->isTaskList()) {
                            m_html.push_back(QStringLiteral(" class=\"contains-task-list\""));
                        }

                        m_html.push_back(QStringLiteral(" dir=\"auto\">\n"));
                    }
                }
            }

            onListItem(item, first);

            first = false;
        }
    }

    if (!first) {
        if (!m_justCollectFootnoteRefs) {
            if (type == ListItem::Ordered) {
                m_html.push_back(QStringLiteral("</ol>\n"));
            } else {
                m_html.push_back(QStringLiteral("</ul>\n"));
            }
        }
    }
}

void HtmlVisitor::onTable(Table *t)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
    }

    if (!t->isEmpty()) {
        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(QStringLiteral("<table"));
            printId(t);
            m_html.push_back(QStringLiteral("><thead><tr>\n"));
        }

        int columns = 0;

        for (auto th = (*t->rows().cbegin())->cells().cbegin(), last = (*t->rows().cbegin())->cells().cend();
             th != last;
             ++th) {
            if (!m_justCollectFootnoteRefs) {
                m_html.push_back(QStringLiteral("<th"));
                m_html.push_back(tableAlignmentToHtml(t->columnAlignment(columns)));
                m_html.push_back(QStringLiteral(" dir=\"auto\">\n"));
            }

            this->onTableCell(th->get());

            if (!m_justCollectFootnoteRefs) {
                m_html.push_back(QStringLiteral("\n</th>\n"));
            }

            ++columns;
        }

        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(QStringLiteral("</tr></thead><tbody>\n"));
        }

        for (auto r = std::next(t->rows().cbegin()), rlast = t->rows().cend(); r != rlast; ++r) {
            if (!m_justCollectFootnoteRefs) {
                m_html.push_back(QStringLiteral("<tr>\n"));
            }

            int i = 0;

            for (auto c = (*r)->cells().cbegin(), clast = (*r)->cells().cend(); c != clast; ++c) {
                if (!m_justCollectFootnoteRefs) {
                    m_html.push_back(QStringLiteral("\n<td"));
                    m_html.push_back(tableAlignmentToHtml(t->columnAlignment(i)));
                    m_html.push_back(QStringLiteral(" dir=\"auto\">\n"));
                }

                this->onTableCell(c->get());

                if (!m_justCollectFootnoteRefs) {
                    m_html.push_back(QStringLiteral("\n</td>\n"));
                }

                ++i;

                if (i == columns) {
                    break;
                }
            }

            if (!m_justCollectFootnoteRefs) {
                for (; i < columns; ++i) {
                    m_html.push_back(QStringLiteral("<td dir=\"auto\"></td>"));
                }

                m_html.push_back(QStringLiteral("\n</tr>\n"));
            }
        }

        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(QStringLiteral("</tbody></table>"));
        }
    }

    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("\n"));
    }
}

void HtmlVisitor::onAnchor(Anchor *a)
{
    if (!m_justCollectFootnoteRefs && m_isWrappedInArticle) {
        m_html.push_back(QStringLiteral("\n<div id=\""));
        m_html.push_back(a->label());
        m_html.push_back(QStringLiteral("\"></div>\n"));
    }
}

void HtmlVisitor::onRawHtml(RawHtml *h)
{
    if (!m_justCollectFootnoteRefs) {
        openStyle(h->openStyles());

        m_html.push_back(h->text());

        closeStyle(h->closeStyles());
    }
}

void HtmlVisitor::onHorizontalLine(HorizontalLine *)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("<hr />"));
    }
}

void HtmlVisitor::onLink(Link *l)
{
    QString url = l->url();

    const auto lit = this->m_doc->labeledLinks().find(url);

    if (lit != this->m_doc->labeledLinks().cend()) {
        url = (*lit)->url();
    }

    if (std::find(this->m_anchors.cbegin(), this->m_anchors.cend(), url) != this->m_anchors.cend()) {
        url = QStringLiteral("#") + url;
    } else if (url.startsWith(QStringLiteral("#"))) {
        const auto it = this->m_doc->labeledHeadings().find(url);

        if (it == this->m_doc->labeledHeadings().cend()) {
            auto path = static_cast<Anchor *>(this->m_doc->items().at(0).get())->label();
            const auto sp = path.lastIndexOf(QStringLiteral("/"));
            path.remove(sp, path.length() - sp);
            const auto p = url.indexOf(path) - 1;
            url.remove(p, url.length() - p);
        } else {
            url = (*it)->label();
        }
    }

    if (!m_justCollectFootnoteRefs) {
        openStyle(l->openStyles());

        m_html.push_back(QStringLiteral("<a href=\""));
        m_html.push_back(url);
        m_html.push_back(QStringLiteral("\""));
        printId(l);
        m_html.push_back(QStringLiteral(">"));
    }

    if (l->p() && !l->p()->isEmpty()) {
        onParagraph(l->p().get(), false);
    } else if (!l->img()->isEmpty()) {
        if (!m_justCollectFootnoteRefs) {
            onImage(l->img().get());
        }
    } else if (!l->text().isEmpty()) {
        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(prepareTextForHtml(l->text()));
        }
    } else {
        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(prepareTextForHtml(l->url()));
        }
    }

    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("</a>"));

        closeStyle(l->closeStyles());
    }
}

void HtmlVisitor::onImage(Image *i)
{
    if (!m_justCollectFootnoteRefs) {
        openStyle(i->openStyles());

        m_html.push_back(QStringLiteral("<img src=\""));
        m_html.push_back(i->url());
        m_html.push_back(QStringLiteral("\" alt=\""));
        m_html.push_back(prepareTextForHtml(i->text()));
        m_html.push_back(QStringLiteral("\" style=\"max-width:100%;\""));
        printId(i);
        m_html.push_back(QStringLiteral(" />"));

        closeStyle(i->closeStyles());
    }
}

void HtmlVisitor::onFootnoteRef(FootnoteRef *ref)
{
    const auto fit = this->m_doc->footnotesMap().find(ref->id());

    if (fit != this->m_doc->footnotesMap().cend()) {
        const auto r = std::find_if(m_fns.begin(), m_fns.end(), [&ref](const auto &stuff) {
            return ref->id() == stuff.m_id;
        });

        if (!m_justCollectFootnoteRefs) {
            openStyle(ref->openStyles());

            m_html.push_back(QStringLiteral("<sup>"));
            m_html.push_back(QStringLiteral("<a href=\"#"));
            m_html.push_back(ref->id());
            m_html.push_back(QStringLiteral("\" id=\"ref-"));
            m_html.push_back(ref->id());
            m_html.push_back(QStringLiteral("-"));
        }

        if (r == m_fns.end()) {
            if (!m_justCollectFootnoteRefs) {
                m_html.push_back(QStringLiteral("1"));
            }
        } else {
            if (!m_justCollectFootnoteRefs) {
                m_html.push_back(QString::number(++(r->m_current)));
            }

            if (!m_dontIncrementFootnoteCount) {
                ++(r->m_count);
            }
        }

        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(QStringLiteral("\">"));
        }

        if (r == m_fns.end()) {
            if (!m_justCollectFootnoteRefs) {
                m_html.push_back(QString::number(m_fns.size() + 1));
            }

            m_fns.push_back({ref->id(), 1, 1});
        } else if (!m_justCollectFootnoteRefs) {
            m_html.push_back(QString::number(std::distance(m_fns.begin(), r) + 1));
        }

        if (!m_justCollectFootnoteRefs) {
            m_html.push_back(QStringLiteral("</a></sup>"));

            closeStyle(ref->closeStyles());
        }
    } else {
        onText(static_cast<Text *>(ref));
    }
}

void HtmlVisitor::onListItem(ListItem *i,
                             bool first,
                             bool skipOpeningWrap)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("<li"));
        printId(i);

        if (i->isTaskList()) {
            skipOpeningWrap = Visitor::wrapFirstParagraphInListItem(i);

            m_html.push_back(QStringLiteral(" class=\"task-list-item\">"));

            if (skipOpeningWrap && !i->isEmpty() && i->items().at(0)->type() == MD::ItemType::Paragraph) {
                m_html.push_back(QStringLiteral("<p dir=\"auto\">"));
            }

            m_html.push_back(
                QStringLiteral("<input "
                               "type=\"checkbox\" id=\"\" disabled=\"\" class=\"task-list-item-checkbox\""));

            if (i->isChecked()) {
                m_html.push_back(QStringLiteral(" checked=\"\""));
            }
        }

        if (i->listType() == ListItem::Ordered && first) {
            m_html.push_back(QStringLiteral(" value=\""));
            m_html.push_back(QString::number(i->startNumber()));
            m_html.push_back(QStringLiteral("\""));
        }

        m_html.push_back(QStringLiteral(">\n"));
    }

    Visitor::onListItem(i, first, skipOpeningWrap);

    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("</li>\n"));
    }
}

void HtmlVisitor::onHeading(Heading *h,
                            const QString &ht)
{
    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("<"));
        m_html.push_back(ht);
        m_html.push_back(headingIdToHtml(h));
        m_html.push_back(QStringLiteral(" dir=\"auto\">"));
    }

    if (h->text().get()) {
        onParagraph(h->text().get(), false);
    }

    if (!m_justCollectFootnoteRefs) {
        m_html.push_back(QStringLiteral("</"));
        m_html.push_back(ht);
        m_html.push_back(QStringLiteral(">"));
    }
}

void HtmlVisitor::onFootnotes(const QString &footnoteBackLinkContent)
{
    if (!m_fns.empty()) {
        m_html.push_back(QStringLiteral("<section class=\"footnotes\"><ol dir=\"auto\">"));
    }

    int i = 1;

    // This copy is needed. First time we want to iterate on non-modified container.
    const auto tmpFns = m_fns;
    m_justCollectFootnoteRefs = true;

    for (const auto &id : tmpFns) {
        const auto fit = this->m_doc->footnotesMap().find(id.m_id);

        if (fit != this->m_doc->footnotesMap().cend()) {
            this->onFootnote(fit->get());
        }
    }

    m_justCollectFootnoteRefs = false;
    m_dontIncrementFootnoteCount = true;

    for (const auto &id : m_fns) {
        m_html.push_back(QStringLiteral("<li id=\""));
        m_html.push_back(id.m_id);
        m_html.push_back(QStringLiteral("\">"));
        ++i;

        const auto fit = this->m_doc->footnotesMap().find(id.m_id);

        if (fit != this->m_doc->footnotesMap().cend()) {
            this->onFootnote(fit->get());

            if (!footnoteBackLinkContent.isEmpty()) {
                QString backRef;
                qsizetype backRefPos = m_html.endsWith(QStringLiteral("</p>")) ? 4 : 0;

                for (qsizetype i = 1; i <= id.m_count; ++i) {
                    backRef.push_back(QStringLiteral("<a href=\"#ref-"));
                    backRef.push_back(id.m_id);
                    backRef.push_back(QStringLiteral("-"));
                    backRef.push_back(QString::number(i));
                    backRef.push_back(QStringLiteral("\">"));
                    backRef.push_back(footnoteBackLinkContent);
                    backRef.push_back(QStringLiteral("</a>"));
                }

                m_html.insert(m_html.length() - backRefPos, backRef);
            }

            m_html.push_back(QStringLiteral("</li>"));
        }
    }

    if (!m_fns.empty()) {
        m_html.push_back(QStringLiteral("</ol></section>\n"));
    }
}

QString HtmlVisitor::headingIdToHtml(Heading *h)
{
    QString html;

    if (h->isLabeled()) {
        html.push_back(QStringLiteral(" id=\""));
        auto label = h->label();
        if (label.startsWith(QStringLiteral("#"))) {
            label.remove(0, 1);
        }
        html.push_back(label);
        html.push_back(QStringLiteral("\""));
    }

    return html;
}

QString HtmlVisitor::prepareTextForHtml(const QString &t)
{
    auto tmp = t;
    tmp.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    tmp.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    tmp.replace(QLatin1Char('>'), QStringLiteral("&gt;"));

    return tmp;
}

QString HtmlVisitor::tableAlignmentToHtml(typename Table::Alignment a)
{
    QString html;

    switch (a) {
    case Table::AlignLeft:
        html.push_back(QStringLiteral(" align=\"left\""));
        break;

    case Table::AlignCenter:
        html.push_back(QStringLiteral(" align=\"center\""));
        break;

    case Table::AlignRight:
        html.push_back(QStringLiteral(" align=\"right\""));
        break;

    // LCOV_EXCL_START
    default:
        break;
        // LCOV_EXCL_STOP
    }

    return html;
}

} /* namespace details */

} /* namespace MD */
