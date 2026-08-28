/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "yaml_parser.h"
#include "text_stream.h"

namespace MD
{

//
// YAMLHeader
//

YAMLHeader::YAMLHeader() = default;

YAMLHeader::~YAMLHeader() = default;

ItemType YAMLHeader::type() const
{
    return static_cast<ItemType>(static_cast<int>(ItemType::UserDefined) + 1);
}

QSharedPointer<Item> YAMLHeader::clone(Document *doc) const
{
    Q_UNUSED(doc)

    auto h = QSharedPointer<YAMLHeader>::create();
    h->applyPositions(*this);
    h->setYaml(yaml());
    h->setStartDelim(startDelim());
    h->setEndDelim(endDelim());

    return h;
}

//
// YAMLParser
//

static const QString s_startString = QStringLiteral("---");
static const QString s_endString = QStringLiteral("...");

YAMLParser::YAMLParser(Parser *parser)
    : BlockParser(parser)
{
}

YAMLParser::~YAMLParser() = default;

BlockState YAMLParser::check(Line &currentLine,
                             TextStream &stream,
                             QSharedPointer<Document>,
                             Context &,
                             const QString &,
                             const QString &,
                             bool checkWithoutProcessing)
{
    const auto sState = stream.currentState();
    const auto lState = currentLine.currentState();

    skipSpaces(currentLine);

    if (currentLine.view().trimmed() == s_startString && currentLine.position() == 0 && lState.m_pos == 0) {
        if (currentLine.lineNumber() != 0) {
            auto line = stream.moveTo(0);

            while (line.lineNumber() < currentLine.lineNumber()) {
                if (!isEmptyLine(line)) {
                    stream.restoreState(&sState);
                    currentLine = stream.currentLine();
                    currentLine.restoreState(&lState);

                    return BlockState::None;
                }

                line = stream.readLine();
            }
        }

        while (!stream.atEnd()) {
            currentLine = stream.readLine();
            const auto str = currentLine.view().trimmed();

            const auto tmpState = currentLine.currentState();
            skipSpaces(currentLine);
            const auto ns = currentLine.position();
            currentLine.restoreState(&tmpState);

            if ((str == s_startString || str == s_endString) && currentLine.position() == 0 && ns == 0) {
                if (checkWithoutProcessing) {
                    stream.restoreState(&sState);
                    currentLine = stream.currentLine();
                    currentLine.restoreState(&lState);
                } else {
                    currentLine.skip();
                }

                return BlockState::Stop;
            }
        }
    }

    stream.restoreState(&sState);
    currentLine = stream.currentLine();
    currentLine.restoreState(&lState);

    return BlockState::None;
}

BlockState YAMLParser::continueCheck(Line &,
                                     TextStream &,
                                     QSharedPointer<Document>,
                                     Context &,
                                     const QString &,
                                     const QString &)
{
    return BlockState::None;
}

BlockState YAMLParser::process(Line &currentLine,
                               TextStream &,
                               QSharedPointer<Document>,
                               QSharedPointer<Block> parent,
                               Context &ctx,
                               const QString &,
                               const QString &,
                               QStringList &)
{
    if (!m_yaml) {
        if (isEmptyLine(currentLine)) {
            currentLine.skip();

            return BlockState::Continue;
        }

        m_yaml = QSharedPointer<YAMLHeader>::create();
        m_yaml->setStartColumn(currentLine.position());
        m_yaml->setStartLine(currentLine.lineNumber());

        m_yaml->setStartDelim(
            {currentLine.position(), currentLine.lineNumber(), currentLine.length() - 1, currentLine.lineNumber()});
        parent->appendItem(m_yaml);
        ctx.setItem(m_yaml.get());

        return BlockState::Continue;
    } else {
        const auto str = currentLine.view().trimmed();

        if (str == s_startString || str == s_endString) {
            m_yaml->setEndDelim(
                {currentLine.position(), currentLine.lineNumber(), currentLine.length() - 1, currentLine.lineNumber()});
            m_yaml->setEndColumn(currentLine.length() - 1);
            m_yaml->setEndLine(currentLine.lineNumber());

            currentLine.skip();

            return BlockState::Stop;
        }

        QString yaml;

        if (!m_yaml->yaml().isEmpty()) {
            yaml.append(s_newLineChar);
        }

        yaml.append(currentLine.slicedCopy(0));

        m_yaml->setYaml(m_yaml->yaml() + yaml);

        return BlockState::Continue;
    }
}

void YAMLParser::reset(Context &)
{
    resetOnAllContexts();
}

void YAMLParser::resetOnAllContexts()
{
    m_yaml.reset();
}

bool YAMLParser::mayBreakParagraph(Line &,
                                   TextStream &,
                                   QSharedPointer<Document>,
                                   Context &) const
{
    return false;
}

} /* namespace MD */
