/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_PARSER_H_INCLUDED
#define MD4QT_MD_PARSER_H_INCLUDED

// md4qt include.
#include "block_parser.h"
#include "doc.h"
#include "inline_parser.h"

// C++ include.
#include <type_traits>

// Qt include.
#include <QHash>

QT_BEGIN_NAMESPACE
class QTextStream;
QT_END_NAMESPACE

/*!
 * \namespace MD
 * \inmodule md4qt
 * \inheaderfile md4qt/parser.h
 *
 * \brief Main namespace of md4qt library.
 *
 * All classes and functions of \c {md4qt} library is defined in this namespace.
 */
namespace MD
{

class Context;
class TextStream;
class Line;

//
// Parser
//

/*!
 * \class MD::Parser
 * \inmodule md4qt
 * \inheaderfile md4qt/parser.h
 *
 * \brief Markdown parser.
 *
 * Parser of Markdown.
 */
class Parser final
{
public:
    /*!
     * \enum MD::Parser::AutolinkUriValidation
     * \inmodule md4qt
     * \inheaderfile md4qt/parser.h
     *
     * \brief Autolink URI validation mode.
     *
     * \value QUrl Use Qt's QUrl validation.
     * \value CommonMark Use CommonMark's absolute URI grammar.
     */
    enum class AutolinkUriValidation {
        QUrl,
        CommonMark
    }; // enum class AutolinkUriValidation

    /*!
     * Default constructor.
     *
     * GitHub's autolinks extension will be added by default.
     */
    Parser();
    ~Parser();

    /*!
     * Returns Parsed Markdown document.
     *
     * \a fileName File name of the Markdown document.
     *
     * \a recursive Should parsing be recursive? If recursive all links to existing Markdown
     *              files will be parsed and presented in the returned document.
     *
     * \a ext Allowed extensions for Markdonw document files. If Markdown file doesn't
     *        have given extension it will be ignored.
     */
    QSharedPointer<Document> parse(const QString &fileName,
                                   bool recursive = true,
                                   const QStringList &ext = {QStringLiteral("md"),
                                                             QStringLiteral("markdown")});

    /*!
     * Returns parsed Markdown document.
     *
     * \a stream Stream to parse.
     *
     * \a path Absolute path to the root folder for the document.
     *         This path will be used to resolve local links.
     *
     * \a fileName This argument needed only for anchor.
     */
    QSharedPointer<Document> parse(QTextStream &stream,
                                   const QString &path,
                                   const QString &fileName);

    /*!
     * Returns parsed Markdown document.
     *
     * \a fileName File name of the Markdown document (full path).
     *
     * \a workingDirectory Absolute path to the working directory for the document.
     *                     This path will be used to resolve local links.
     *                     \warning This path should be in \a fileName path.
     *
     * \a recursive Should parsing be recursive? If recursive all links to existing Markdown
     *              files will be parsed and presented in the returned document.
     *
     * \a ext Allowed extensions for Markdonw document files. If Markdown file doesn't
     *        have given extension it will be ignored.
     */
    QSharedPointer<Document> parse(const QString &fileName,
                                   const QString &workingDirectory,
                                   bool recursive = true,
                                   const QStringList &ext = {QStringLiteral("md"),
                                                             QStringLiteral("markdown")});

    /*!
     * Returns autolink URI validation mode.
     */
    inline AutolinkUriValidation autolinkUriValidation() const
    {
        return m_autolinkUriValidation;
    }

    /*!
     * Sets autolink URI validation mode.
     *
     * QUrl validation preserves historical md4qt behavior, while CommonMark
     * accepts all absolute URIs allowed by the CommonMark autolink grammar.
     *
     * \a validation Validation mode.
     */
    inline void setAutolinkUriValidation(AutolinkUriValidation validation)
    {
        m_autolinkUriValidation = validation;
    }

    /*!
     * \inmodule md4qt
     * \typealias MD::Parser::BlockParsers
     * \inheaderfile md4qt/parser.h
     *
     * \brief Vector of block parsers.
     */
    using BlockParsers = QVector<QSharedPointer<BlockParser>>;

    /*!
     * \inmodule md4qt
     * \typealias MD::Parser::InlineParsers
     * \inheaderfile md4qt/parser.h
     *
     * \brief Vector of inline parsers.
     */
    using InlineParsers = QVector<QSharedPointer<InlineParser>>;

    /*!
     * Adds new block parser to pipeline.
     *
     * \a parsers Receiver.
     *
     * \a parser Parent.
     */
    template<class T>
    static inline typename std::enable_if<std::is_base_of<BlockParser,
                                                          T>::value
                                          && !std::is_base_of<InlineParser,
                                                              T>::value>::type
    appendBlockParser(BlockParsers &parsers,
                      Parser *parser)
    {
        parsers.append(QSharedPointer<T>::create(parser));
    }

    /*!
     * Adds new inline parser into pipeline.
     *
     * \a parsers Receiver.
     */
    template<class T,
             std::enable_if_t<std::is_base_of<InlineParser,
                                              T>::value,
                              bool> = true>
    static QSharedPointer<T> appendInlineParser(InlineParsers &parsers)
    {
        auto p = QSharedPointer<T>::create();

        parsers.append(p);

        return p;
    }

    /*!
     * Returns current block parsers pipeline.
     */
    inline const BlockParsers &blockParsers() const
    {
        return m_blockParsers;
    }

    /*!
     * Set current block parsers pipeline.
     *
     * \a p Pipeline.
     */
    inline void setBlockParsers(const BlockParsers &p)
    {
        m_blockParsers = p;
    }

    /*!
     * Returms inline parsers for the given opener symbol.
     *
     * \a c Opener symbol.
     */
    inline InlineParsers inlineParsersFor(const QChar &c) const
    {
        return m_inlineParsers.value(c);
    }

    /*!
     * Push state of all inline parsers.
     */
    inline void pushStateOfInliners()
    {
        for (const auto &p : std::as_const(m_allInlineParsers)) {
            p->pushState();
        }
    }

    /*!
     * Pop state of all inline parsers.
     */
    inline void popStateOfInliners()
    {
        for (const auto &p : std::as_const(m_allInlineParsers)) {
            p->popState();
        }
    }

    /*!
     * Set current inline parsers pipeline.
     *
     * \a p Pipeline.
     */
    inline void setInlineParsers(const InlineParsers &p)
    {
        m_allInlineParsers = p;

        m_inlineParsers.clear();

        for (const auto &inl : p) {
            const auto chars = inl->startDelimiterSymbols();

            for (qsizetype i = 0; i < chars.size(); ++i) {
                m_inlineParsers[chars[i]].append(inl);
            }
        }
    }

    /*!
     * Makes default pipeline of block parsers.
     *
     * \a parser Parent.
     */
    static BlockParsers makeDefaultBlockParsersPipeline(Parser *parser);

    /*!
     * Makes default pipeline of inline parsers.
     */
    static InlineParsers makeDefaultInlineParsersPipeline();

    /*!
     * Makes CommonMark pipeline of block parsers.
     *
     * \a parser Parent.
     */
    static BlockParsers makeCommonMarkBlockParsersPipeline(Parser *parser);

    /*!
     * Makes CommonMark pipeline of inline parsers.
     */
    static InlineParsers makeCommonMarkInlineParsersPipeline();

    /*!
     * Returns block parser for the given line.
     *
     * \note During this check \a line's position won't be moved accordingly.
     *
     * \a line Line.
     *
     * \a stream Stream.
     *
     * \a doc Document.
     *
     * \a ctx Context.
     */
    BlockParser *checkBlock(Line &line,
                            TextStream &stream,
                            QSharedPointer<Document> doc,
                            Context &ctx) const;

    /*!
     * Returns block parser for the given line excluding the given block parser.
     *
     * \note During this check \a line's position won't be moved accordingly.
     *
     * \a line Line.
     *
     * \a stream Stream.
     *
     * \a doc Document.
     *
     * \a ctx Context.
     *
     * \a exclude Block parser that should be excluded.
     */
    BlockParser *checkBlockExcluding(Line &line,
                                     TextStream &stream,
                                     QSharedPointer<Document> doc,
                                     Context &ctx,
                                     const BlockParser *exclude) const;

private:
    void parseFile(const QString &fileName,
                   bool recursive,
                   QSharedPointer<Document> doc,
                   const QStringList &ext,
                   QStringList *parentLinks = nullptr,
                   QString workingDirectory = {});

    void parseStream(QTextStream &stream,
                     const QString &path,
                     const QString &fileName,
                     bool recursive,
                     QSharedPointer<Document> doc,
                     const QStringList &ext,
                     QStringList *parentLinks = nullptr,
                     const QString &workingDirectory = {});

    // Both phases.
    void parse(QTextStream &s,
               QSharedPointer<Document> doc,
               const QString &path,
               const QString &fileName,
               QStringList &linksToParse);

    struct ParseState {
        BlockState m_state = BlockState::None;
        Context *m_context = nullptr;
        QSet<BlockParser *> m_skip;
    };

    // First phase.
    void parse(Line &currentLine,
               TextStream &stream,
               QSharedPointer<Document> doc,
               Context &ctx,
               const QString &path,
               const QString &fileName,
               QStringList &linksToParse,
               ParseState &state);

    // Loop through all block parsers.
    void loopBlockParsers(Line &currentLine,
                          TextStream &stream,
                          QSharedPointer<Document> doc,
                          Context &ctx,
                          const QString &path,
                          const QString &fileName,
                          QStringList &linksToParse,
                          ParseState &state);

    // Reset parsers - invokes reset() moethod for each of them.
    void resetParsers();

    // Reset parser's state (including all pipelines).
    void reset();

private:
    QStringList m_parsedFiles;
    QVector<QSharedPointer<BlockParser>> m_blockParsers;
    InlineParsers m_allInlineParsers;
    QHash<QChar, InlineParsers> m_inlineParsers;
    AutolinkUriValidation m_autolinkUriValidation = AutolinkUriValidation::QUrl;

    Q_DISABLE_COPY(Parser)
}; // class Parser

} /* namespace MD */

#endif // MD4QT_MD_PARSER_H_INCLUDED
