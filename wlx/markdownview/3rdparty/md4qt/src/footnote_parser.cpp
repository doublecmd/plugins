/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "footnote_parser.h"
#include "constants.h"
#include "reverse_solidus.h"
#include "utils.h"

namespace MD
{

inline bool isFootnote(Line &line)
{
    auto roll = line.makeRollbackObject();

    skipSpaces(line);

    if (line.length() - line.position() < 5) {
        return false;
    }

    if (line.currentChar() != s_leftSquareBracketChar) {
        return false;
    }

    line.nextChar();

    if (line.currentChar() != s_circumflexAccentifChar) {
        return false;
    }

    line.nextChar();

    if (line.currentChar() == s_rightSquareBracketChar || line.currentChar().isSpace()) {
        return false;
    }

    ReverseSolidusHandler rs;

    while (line.position() < line.length()) {
        if (rs.isNotEscaped(line.currentChar())) {
            if (line.currentChar() == s_rightSquareBracketChar) {
                break;
            } else if (line.currentChar().isSpace()) {
                return false;
            }
        }

        line.nextChar();
        rs.next();
    }

    line.nextChar();

    if (line.position() < line.length() && line.currentChar() == s_colonChar) {
        line.nextChar();

        if (line.currentChar().isSpace()) {
            roll.enable(false);

            return true;
        }
    }

    return false;
}

FootnoteParser::FootnoteParser(Parser *parser)
    : BlockParser(parser)
{
}

FootnoteParser::~FootnoteParser() = default;

BlockState FootnoteParser::check(Line &currentLine,
                                 TextStream &,
                                 QSharedPointer<Document> doc,
                                 Context &ctx,
                                 const QString &path,
                                 const QString &fileName,
                                 bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (ctx.isInIndent(currentLine.column())) {
        if (isFootnote(currentLine)) {
            currentLine.restoreState();

            if (!checkWithoutProcessing) {
                processLabel(currentLine, doc, path, fileName, nullptr);

                ctx.appendChildIndent(indentFromColumn(currentLine.column()));

                if (currentLine.currentChar() == s_spaceChar) {
                    currentLine.nextChar();
                }
            }

            return BlockState::Continue;
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState FootnoteParser::continueCheck(Line &currentLine,
                                         TextStream &stream,
                                         QSharedPointer<Document> doc,
                                         Context &ctx,
                                         const QString &path,
                                         const QString &fileName)
{
    currentLine.saveState();

    if (isEmptyLine(currentLine)) {
        m_wasEmptyLine = true;

        return BlockState::Continue;
    } else {
        if (m_wasEmptyLine) {
            ctx.childIndents().clear();
            ctx.appendChildIndent(4);

            while (currentLine.position() < currentLine.length()
                   && currentLine.column() < indentFromColumn(ctx.indentColumnForCheck(false))
                   && currentLine.currentChar().isSpace()) {
                currentLine.nextChar();
            }

            if (currentLine.column() >= indentFromColumn(ctx.indentColumnForCheck(false))) {
                return BlockState::Continue;
            } else {
                return BlockState::Stop;
            }
        } else {
            const auto child = ctx.childIndents();
            ctx.childIndents().clear();

            if (check(currentLine, stream, doc, ctx, path, fileName, true) == BlockState::None) {
                ctx.childIndents() = child;

                return BlockState::Continue;
            } else {
                return BlockState::Stop;
            }
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

void FootnoteParser::processLabel(Line &currentLine,
                                  QSharedPointer<Document> doc,
                                  const QString &path,
                                  const QString &fileName,
                                  QSharedPointer<Footnote> note)
{
    skipSpaces(currentLine);
    const auto startPos = currentLine.position();
    currentLine.nextChar();

    QString label;

    ReverseSolidusHandler rs;

    while (currentLine.position() < currentLine.length()) {
        if (rs.isNotEscaped(currentLine.currentChar())) {
            if (currentLine.currentChar() == s_rightSquareBracketChar) {
                break;
            } else {
                label.append(currentLine.currentChar());
            }
        }

        currentLine.nextChar();
        rs.next();
    }

    currentLine.nextChar();
    const auto endPos = currentLine.position();
    currentLine.nextChar();

    if (note) {
        note->setIdPos({startPos, currentLine.lineNumber(), endPos, currentLine.lineNumber()});
    }

    label = s_numberSignChar
        + label.toCaseFolded().toUpper()
        + s_solidusChar
        + (!path.isEmpty() ? path + s_solidusChar : QString())
        + fileName;

    doc->insertFootnote(label, note);
}

BlockState FootnoteParser::process(Line &currentLine,
                                   TextStream &stream,
                                   QSharedPointer<Document> doc,
                                   QSharedPointer<Block>,
                                   Context &ctx,
                                   const QString &path,
                                   const QString &fileName,
                                   QStringList &linksToParse)
{
    currentLine.saveState();

    if (!m_note) {
        if (isEmptyLine(currentLine)) {
            currentLine.skip();

            return BlockState::Continue;
        }

        m_note = QSharedPointer<Footnote>::create();
        m_note->setStartColumn(currentLine.position());
        m_note->setStartLine(currentLine.lineNumber());
        m_note->setEndColumn(currentLine.length() - 1);
        m_note->setEndLine(currentLine.lineNumber());
        ctx.setItem(m_note.get());

        processLabel(currentLine, doc, path, fileName, m_note);

        ctx.appendChildIndent(indentFromColumn(currentLine.column()));

        BlockParser::process(currentLine, stream, doc, m_note, ctx, path, fileName, linksToParse);

        ctx.childIndents().clear();

        return BlockState::Continue;
    } else {
        const auto empty = isEmptyLine(currentLine);

        if (m_wasEmptyLine) {
            if (ctx.childIndents().isEmpty()) {
                ctx.appendChildIndent(4);
            }

            const auto st = currentLine.currentState();

            while (currentLine.position() < currentLine.length()
                   && currentLine.column() < indentFromColumn(ctx.indentColumnForCheck(false))
                   && currentLine.currentChar().isSpace()) {
                currentLine.nextChar();
            }

            if (empty || currentLine.column() >= indentFromColumn(ctx.indentColumnForCheck(false))) {
                if (!empty) {
                    m_note->setEndColumn(currentLine.length() - 1);
                    m_note->setEndLine(currentLine.lineNumber());
                }

                BlockParser::process(currentLine, stream, doc, m_note, ctx, path, fileName, linksToParse);

                return BlockState::Continue;
            } else {
                currentLine.restoreState(&st);

                return BlockState::Stop;
            }
        } else {
            if (empty) {
                m_wasEmptyLine = true;
            }

            const auto child = ctx.childIndents();
            ctx.childIndents().clear();

            if (check(currentLine, stream, doc, ctx, path, fileName, true) == BlockState::None) {
                ctx.childIndents() = child;

                BlockParser::process(currentLine, stream, doc, m_note, ctx, path, fileName, linksToParse);

                return BlockState::Continue;
            } else {
                return BlockState::Stop;
            }
        }
    }

    currentLine.restoreState();

    return BlockState::Stop;
}

void FootnoteParser::reset(Context &ctx)
{
    resetOnAllContexts();

    ctx.childIndents().clear();
}

void FootnoteParser::resetOnAllContexts()
{
    m_note.reset();
    m_wasEmptyLine = false;
}

bool FootnoteParser::mayBreakParagraph(Line &,
                                       TextStream &,
                                       QSharedPointer<Document>,
                                       Context &) const
{
    return true;
}

void FootnoteParser::finish(Line &currentLine,
                            TextStream &stream,
                            QSharedPointer<Document> doc,
                            QSharedPointer<Block>,
                            Context &ctx,
                            const QString &path,
                            const QString &fileName,
                            QStringList &linksToParse)
{
    if (m_note && ctx.children().size() && ctx.children().front().block()) {
        BlockParser::process(currentLine, stream, doc, m_note, ctx, path, fileName, linksToParse);
    }
}

} /* namespace MD */
