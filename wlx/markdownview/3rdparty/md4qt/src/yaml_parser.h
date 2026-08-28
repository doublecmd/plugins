/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

#ifndef MD4QT_MD_YAML_PARSER_H_INCLUDED
#define MD4QT_MD_YAML_PARSER_H_INCLUDED

// md4qt include.
#include "block_parser.h"
#include "doc.h"

namespace MD
{

//
// YAMLHeader
//

/*!
 * \class MD::YAMLHeader
 * \inmodule md4qt
 * \inheaderfile md4qt/yaml_parser.h
 *
 * \brief YAML header item in the document.
 */
class YAMLHeader : public Item
{
public:
    YAMLHeader();
    ~YAMLHeader() override;

    /*!
     * Returns type of the item.
     */
    ItemType type() const override;

    /*!
     * Clone this item.
     *
     * \a doc Parent of new item.
     */
    QSharedPointer<Item> clone(Document *doc = nullptr) const override;

    /*!
     * Returns YAML content.
     */
    inline const QString &yaml() const
    {
        return m_yaml;
    }

    /*!
     * Set YAML content.
     *
     * \a y YAML content.
     */
    inline void setYaml(const QString &y)
    {
        m_yaml = y;
    }

    /*!
     * Returns start delimiter position.
     */
    inline const WithPosition &startDelim() const
    {
        return m_startDelim;
    }

    /*!
     * Set start delimiter position.
     *
     * \a p Position.
     */
    inline void setStartDelim(const WithPosition &p)
    {
        m_startDelim = p;
    }

    /*!
     * Returns end delimiter position.
     */
    inline const WithPosition &endDelim() const
    {
        return m_endDelim;
    }

    /*!
     * Set end delimiter position.
     *
     * \a p Position.
     */
    inline void setEndDelim(const WithPosition &p)
    {
        m_endDelim = p;
    }

private:
    /*!
     * YAML content.
     */
    QString m_yaml;
    /*!
     * Start delimiter of YAML block.
     */
    WithPosition m_startDelim;
    /*!
     * End delimitier of YAML block.
     */
    WithPosition m_endDelim;
}; // class YAMLHeader

//
// YAMLParser
//

/*!
 * \class MD::YAMLParser
 * \inmodule md4qt
 * \inheaderfile md4qt/yaml_parser.h
 *
 * \brief YAML parser.
 *
 * YAML parser
 */
class YAMLParser : public BlockParser
{
public:
    explicit YAMLParser(Parser *parser);
    ~YAMLParser() override;

    /*!
     * Returns check state for the current line and block type. This method should return BlockState::None if
     * this string is not handled by him. If this block parser knows how to handle the current line it should
     * return BlockState::Continue or BlockState::Stop.
     *
     * \a currentLine Current processing line.
     *
     * \a stream Stream of the lines.
     *
     * \a doc Current document.
     *
     * \a ctx Current parsing context.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     *
     * \a checkWithoutProcessing Flags to don't do real processing, just check this parser handles this line.
     */
    BlockState check(Line &currentLine,
                     TextStream &stream,
                     QSharedPointer<Document> doc,
                     Context &ctx,
                     const QString &path,
                     const QString &fileName,
                     bool checkWithoutProcessing = false) override;

    /*!
     * Continue check. If checking knows that block is finished it should return BlockState::Stop.
     *
     * \a currentLine Current processing line.
     *
     * \a stream Stream of the lines.
     *
     * \a doc Current document.
     *
     * \a ctx Current parsing context.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     */
    BlockState continueCheck(Line &currentLine,
                             TextStream &stream,
                             QSharedPointer<Document> doc,
                             Context &ctx,
                             const QString &path,
                             const QString &fileName) override;

    /*!
     * Process block - do actual parsing and populating MD::Document. If starting empty line is not a subject
     * for work for this kind of block this method should just skip the line. When parsing of a block is finished
     * this method should return BlockState::Stop.
     *
     * This method should call himself on all children contexts till block finish.
     *
     * \a currentLine Current processing line.
     *
     * \a stream Stream of the lines.
     *
     * \a doc Document.
     *
     * \a parent Parent of this block.
     *
     * \a ctx Current parsing context.
     *
     * \a path Path to Markdown file.
     *
     * \a fileName File name of the Markdown file.
     *
     * \a linksToParse List of links for further parsing.
     */
    BlockState process(Line &currentLine,
                       TextStream &stream,
                       QSharedPointer<Document> doc,
                       QSharedPointer<Block> parent,
                       Context &ctx,
                       const QString &path,
                       const QString &fileName,
                       QStringList &linksToParse) override;

    /*!
     * Reset state of the block parser for further reuse on the given context.
     *
     * \a ctx Context.
     */
    void reset(Context &ctx) override;

    /*!
     * Reset state of the block parser for further reuse.
     */
    void resetOnAllContexts() override;

    /*!
     * Return whether this kind of block may break a paragraph.
     *
     * \a line Line.
     *
     * \a stream Stream.
     *
     * \a doc Document.
     *
     * \a ctx Context.
     */
    bool mayBreakParagraph(Line &line,
                           TextStream &stream,
                           QSharedPointer<Document> doc,
                           Context &ctx) const override;

private:
    QSharedPointer<YAMLHeader> m_yaml;
}; // class YAMLParser

} /* namespace MD */

#endif // MD4QT_MD_YAML_PARSER_H_INCLUDED
