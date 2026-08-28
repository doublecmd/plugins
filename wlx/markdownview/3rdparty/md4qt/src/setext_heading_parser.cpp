/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "setext_heading_parser.h"
#include "atx_heading_parser.h"
#include "constants.h"
#include "context.h"
#include "list_parser.h"
#include "paragraph_parser.h"
#include "utils.h"

namespace MD
{

SetextHeadingParser::SetextHeadingParser(Parser *parser)
    : BlockParser(parser)
{
}

SetextHeadingParser::~SetextHeadingParser() = default;

inline bool isSetextUnderline(Line &line)
{
    const auto rollback = line.makeRollbackObject();

    const auto c = line.currentChar();

    if (c != s_equalSignChar && c != s_minusChar) {
        return false;
    }

    skipIf(line, [&c](const QChar &ch) {
        return (ch == c);
    });

    skipSpaces(line);

    if (!line.atEnd()) {
        return false;
    }

    return true;
}

bool SetextHeadingParser::isSetext(Line &line,
                                   Context &ctx)
{
    auto p = dynamic_cast<ParagraphParser *>(ctx.block());

    if (p && ctx.startPos(line.lineNumber() - 1).m_pos != -1 && ctx.firstLineNumber() != -1) {
        const auto roll = line.makeRollbackObject();

        skipSpaces(line);

        if (line.column() >= indentFromColumn(ctx.indentColumn())
            && line.column() < ctx.indentColumn() + 4
            && isSetextUnderline(line)) {
            return true;
        }
    }

    return false;
}

BlockState SetextHeadingParser::check(Line &currentLine,
                                      TextStream &,
                                      QSharedPointer<Document>,
                                      Context &ctx,
                                      const QString &,
                                      const QString &,
                                      bool checkWithoutProcessing)
{
    if (ctx.block()) {
        if (isSetext(currentLine, ctx)) {
            if (!checkWithoutProcessing) {
                currentLine.skip();
            }

            return BlockState::Stop;
        } else {
            auto list = dynamic_cast<ListParser *>(ctx.block());

            if (list && !ctx.children().isEmpty()) {
                if (isSetext(currentLine, ctx.children().back())) {
                    if (!checkWithoutProcessing) {
                        currentLine.skip();
                    }

                    return BlockState::Stop;
                }
            }
        }
    } else if (ctx.parent() && ctx.parent()->children().size() > 1) {
        if (isSetext(currentLine, ctx.parent()->children()[ctx.parent()->children().size() - 2])) {
            if (!checkWithoutProcessing) {
                currentLine.skip();
            }

            return BlockState::Stop;
        }
    }

    return BlockState::None;
}

BlockState SetextHeadingParser::continueCheck(Line &,
                                              TextStream &,
                                              QSharedPointer<Document>,
                                              Context &,
                                              const QString &,
                                              const QString &)
{
    return BlockState::Stop;
}

BlockState SetextHeadingParser::process(Line &currentLine,
                                        TextStream &,
                                        QSharedPointer<Document> doc,
                                        QSharedPointer<Block> parent,
                                        Context &,
                                        const QString &path,
                                        const QString &fileName,
                                        QStringList &)
{
    auto heading = QSharedPointer<Heading>::create();
    auto p = parent->items().back().staticCast<Paragraph>();
    parent->removeItemAt(parent->items().size() - 1);
    heading->setStartColumn(p->startColumn());
    heading->setStartLine(p->startLine());
    heading->setEndColumn(currentLine.length() - 1);
    heading->setEndLine(currentLine.lineNumber());
    heading->setText(p);
    parent->appendItem(heading);

    skipSpaces(currentLine);

    if (currentLine.currentChar() == s_equalSignChar) {
        heading->setLevel(1);
    } else {
        heading->setLevel(2);
    }

    const auto startDelimPos = currentLine.position();
    const auto dch = currentLine.currentChar();

    skipIf(currentLine, [&dch](const QChar &ch) {
        return dch == ch;
    });

    Heading::Delims delims;
    delims.append({startDelimPos, currentLine.lineNumber(), currentLine.position() - 1, currentLine.lineNumber()});
    heading->setDelims(delims);

    ATXHeadingParser::processLabel(p, path, fileName, heading, doc);

    return BlockState::Stop;
}

void SetextHeadingParser::reset(Context &)
{
}

void SetextHeadingParser::resetOnAllContexts()
{
}

bool SetextHeadingParser::mayBreakParagraph(Line &,
                                            TextStream &,
                                            QSharedPointer<Document>,
                                            Context &) const
{
    return true;
}

bool SetextHeadingParser::canBeLazyLine(Line &line,
                                        TextStream &,
                                        QSharedPointer<Document>,
                                        Context &) const
{
    const auto rollback = line.makeRollbackObject();

    skipSpaces(line);

    return (line.currentChar() == s_equalSignChar);
}

} /* namespace MD */
