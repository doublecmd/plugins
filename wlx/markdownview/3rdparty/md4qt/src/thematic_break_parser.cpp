/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "thematic_break_parser.h"
#include "utils.h"

namespace MD
{

ThematicBreakParser::ThematicBreakParser(Parser *parser)
    : BlockParser(parser)
{
}

ThematicBreakParser::~ThematicBreakParser() = default;

BlockState ThematicBreakParser::check(Line &currentLine,
                                      TextStream &,
                                      QSharedPointer<Document>,
                                      Context &ctx,
                                      const QString &,
                                      const QString &,
                                      bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (currentLine.column() - ctx.indentColumnForCheck(false) < 4 && isHorizontalLine(currentLine)) {
        if (checkWithoutProcessing) {
            currentLine.restoreState();
        }

        return BlockState::Stop;
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState ThematicBreakParser::continueCheck(Line &,
                                              TextStream &,
                                              QSharedPointer<Document>,
                                              Context &,
                                              const QString &,
                                              const QString &)
{
    return BlockState::Stop;
}

BlockState ThematicBreakParser::process(Line &currentLine,
                                        TextStream &,
                                        QSharedPointer<Document>,
                                        QSharedPointer<Block> parent,
                                        Context &,
                                        const QString &,
                                        const QString &,
                                        QStringList &)
{
    if (isEmptyLine(currentLine)) {
        currentLine.skip();

        return BlockState::Continue;
    }

    auto hl = QSharedPointer<HorizontalLine>::create();
    hl->setStartColumn(currentLine.position());
    hl->setStartLine(currentLine.lineNumber());
    hl->setEndColumn(currentLine.length() - 1);
    hl->setEndLine(currentLine.lineNumber());
    parent->appendItem(hl);
    currentLine.skip();

    return BlockState::Stop;
}

void ThematicBreakParser::reset(Context &)
{
}

void ThematicBreakParser::resetOnAllContexts()
{
}

bool ThematicBreakParser::mayBreakParagraph(Line &,
                                            TextStream &,
                                            QSharedPointer<Document>,
                                            Context &) const
{
    return true;
}

} /* namespace MD */
