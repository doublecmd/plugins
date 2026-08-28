/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_LINK_IMAGE_PARSER_H_INCLUDED
#define MD4QT_MD_LINK_IMAGE_PARSER_H_INCLUDED

// md4qt include.
#include "inline_parser.h"
#include "text_stream.h"

// Qt include.
#include <QStack>
#include <QVector>

namespace MD
{

/*!
 * \class MD::LinkImageParser
 * \inmodule md4qt
 * \inheaderfile md4qt/link_image_parser.h
 *
 * \brief Link and image parser.
 *
 * Link and image parser.
 */
class LinkImageParser : public InlineParser
{
public:
    LinkImageParser();
    ~LinkImageParser() override;

    /*!
     * Check if current character (string) is handled by this parser.
     *
     * Returns true if handled, false otherwise.
     *
     * This method should do a work with context and handle his delimiters
     * and cache in context ready for use object that will be placed into MD::Paragraph.
     *
     * \a line Current line.
     *
     * \a stream Stream.
     *
     * \a ctx Context.
     *
     * \a doc Document.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     *
     * \a linksToParse List of links for further parsing.
     *
     * \a parser Parser.
     *
     * \a rs Auxiliary struct for checking if first charcter is escaped.
     */
    bool check(Line &line,
               ParagraphStream &stream,
               InlineContext &ctx,
               QSharedPointer<Document> doc,
               const QString &path,
               const QString &fileName,
               QStringList &linksToParse,
               Parser &parser,
               const ReverseSolidusHandler &rs) override;

    /*!
     * Push state of the parser on stack. For some parsers this is needed, as links, for example,
     * parses its description part as paragraph. This is a subject for implementation of parser...
     */
    void pushState() override;

    /*!
     * Pop state of the parser from stack.
     *
     * \sa pushState()
     */
    void popState() override;

    /*!
     * Returns string with all symbols that can start this kind of inline.
     */
    QString startDelimiterSymbols() const override;

private:
    struct State {
        struct Delim {
            enum Type {
                Image,
                Link,
                RightSquare
            };

            ParagraphStream::State m_streamState;
            Line::State m_lineState;
            Type m_type;
            bool m_active = true;
        };

        QVector<Delim> m_openers;
    };

    QStack<State> m_states;

private:
    qsizetype findOpener();
    void clearLinkOpenersAndCtx(qsizetype idx,
                                const State::Delim &startDelim,
                                const ParagraphStream::State &endStreamState,
                                const Line::State &endLineState,
                                InlineContext &ctx);
    QPair<bool,
          bool>
    checkShortcutLinkImage(const State::Delim &startDelim,
                           const ParagraphStream::State &endStreamState,
                           const Line::State &endLineState,
                           ParagraphStream &stream,
                           InlineContext &ctx,
                           QSharedPointer<Document> doc,
                           const QString &path,
                           const QString &fileName,
                           QStringList &linksToParse,
                           Parser &parser);
    bool checkInlineLinkImage(const State::Delim &startDelim,
                              const ParagraphStream::State &endDescStreamState,
                              const Line::State &endDescLineState,
                              const ParagraphStream::State &endStreamState,
                              const Line::State &endLineState,
                              ParagraphStream &stream,
                              InlineContext &ctx,
                              QSharedPointer<Document> doc,
                              const QString &path,
                              const QString &fileName,
                              QStringList &linksToParse,
                              Parser &parser);
    bool checkRefLinkImage(const State::Delim &startDelim,
                           const ParagraphStream::State &endDescStreamState,
                           const Line::State &endDescLineState,
                           const ParagraphStream::State &endStreamState,
                           const Line::State &endLineState,
                           ParagraphStream &stream,
                           InlineContext &ctx,
                           QSharedPointer<Document> doc,
                           const QString &path,
                           const QString &fileName,
                           QStringList &linksToParse,
                           Parser &parser);
    void makeLink(const QString &url,
                  const QString &title,
                  QStringList &linksToParse,
                  const QString &path,
                  const QString &fileName,
                  const State::Delim &startParagraphDelim,
                  const State::Delim &endParagraphDelim,
                  ParagraphStream &stream,
                  InlineContext &ctx,
                  const WithPosition &textPos,
                  const WithPosition &urlPos,
                  Parser &parser,
                  QSharedPointer<Document> doc,
                  qsizetype startLinkPos,
                  qsizetype startLinkLine,
                  qsizetype endLinkPos,
                  qsizetype endLinkLine);
    void makeImage(const QString &url,
                   const QString &title,
                   QStringList &linksToParse,
                   const QString &path,
                   const QString &fileName,
                   const State::Delim &startParagraphDelim,
                   const State::Delim &endParagraphDelim,
                   ParagraphStream &stream,
                   InlineContext &ctx,
                   const WithPosition &textPos,
                   const WithPosition &urlPos,
                   Parser &parser,
                   QSharedPointer<Document> doc,
                   qsizetype startLinkPos,
                   qsizetype startLinkLine,
                   qsizetype endLinkPos,
                   qsizetype endLinkLine);
    QString prepareUrl(const QString &url,
                       QStringList &linksToParse,
                       const QString &path,
                       const QString &fileName);
    QPair<QSharedPointer<Paragraph>,
          QString>
    parseDescription(const State::Delim &startParagraphDelim,
                     const State::Delim &endParagraphDelim,
                     ParagraphStream &stream,
                     Parser &parser,
                     QSharedPointer<Document> doc,
                     const QString &path,
                     const QString &fileName,
                     QStringList &linksToParse);
    QString readLabel(const State::Delim &startDelim,
                      const ParagraphStream::State &endStreamState,
                      const Line::State &endLineState,
                      ParagraphStream &stream,
                      qsizetype &startLabelPos,
                      qsizetype &startLabelLine,
                      qsizetype &endLabelPos,
                      qsizetype &endLabelLine,
                      State::Delim &startParagraphDelim,
                      State::Delim &endParagraphDelim,
                      qsizetype &startTextPos,
                      qsizetype &startTextLine,
                      qsizetype &endTextPos,
                      qsizetype &endTextLine);
    void setImgAndP(const QPair<QSharedPointer<Paragraph>,
                                QString> &p,
                    QSharedPointer<Link> link);
}; // class LinkImageParser

} /* namespace MD */

#endif // MD4QT_MD_LINK_IMAGE_PARSER_H_INCLUDED
