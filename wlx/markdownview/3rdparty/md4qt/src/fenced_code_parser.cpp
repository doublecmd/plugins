/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include
#include "fenced_code_parser.h"
#include "constants.h"
#include "utils.h"

namespace MD
{

FencedCodeParser::FencedCodeParser(Parser *parser)
    : BlockParser(parser)
{
}

FencedCodeParser::~FencedCodeParser() = default;

void parseStartOfCode(Line &line,
                      QString *syntax = nullptr,
                      WithPosition *delim = nullptr,
                      WithPosition *syntaxPos = nullptr)
{
    if (delim) {
        delim->setStartColumn(line.column());
        delim->setStartLine(line.lineNumber());
    }

    const auto fc = line.currentChar();
    const bool c96 = (line.currentChar() == s_graveAccentChar);
    const bool c126 = (line.currentChar() == s_tildeChar);

    if (c96 || c126) {
        line.nextChar();
        int c = 1;

        while (line.currentChar() == fc) {
            line.nextChar();
            ++c;
        }

        if (delim) {
            delim->setEndColumn(line.position() - 1);
            delim->setEndLine(line.lineNumber());
        }

        if (syntax) {
            skipSpaces(line);
            qsizetype startSyntaxPos = line.position();

            if (line.position() < line.length()) {
                qsizetype endSyntaxPos = -1;
                *syntax = readEscapedSequence(line, &endSyntaxPos);
                replaceEntity(*syntax);
                removeBackslashes(*syntax);

                if (syntaxPos) {
                    syntaxPos->setStartColumn(startSyntaxPos);
                    syntaxPos->setStartLine(line.lineNumber());
                    syntaxPos->setEndColumn(endSyntaxPos);
                    syntaxPos->setEndLine(line.lineNumber());
                }
            }
        }
    }
}

bool isCodeFences(Line &line,
                  qsizetype &count,
                  QChar &fc,
                  bool closing = false)
{
    count = 0;

    const auto rollback = line.makeRollbackObject();

    skipSpaces(line);

    fc = line.currentChar();

    if (fc != s_tildeChar && fc != s_graveAccentChar) {
        return false;
    }

    bool space = false;

    while (line.position() < line.length()) {
        if (line.currentChar().isSpace()) {
            space = true;
        } else if (line.currentChar() == fc) {
            if (space && (closing ? true : fc == s_graveAccentChar)) {
                return false;
            }

            if (!space) {
                ++count;
            }
        } else if (closing) {
            return false;
        } else {
            break;
        }

        line.nextChar();
    }

    if (count < 3) {
        return false;
    }

    if (fc == s_graveAccentChar) {
        const auto p = line.view().indexOf(s_graveAccentChar, line.position());

        if (p != -1) {
            return false;
        }
    }

    return true;
}

BlockState FencedCodeParser::check(Line &currentLine,
                                   TextStream &,
                                   QSharedPointer<Document>,
                                   Context &ctx,
                                   const QString &,
                                   const QString &,
                                   bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    const auto start = currentLine.column();

    if (ctx.isInIndent(currentLine.column())) {
        const auto fenced = isCodeFences(currentLine, m_openCount, m_startChar, false);

        if (fenced) {
            if (checkWithoutProcessing) {
                currentLine.restoreState();
            } else {
                ctx.setFirstLineNumber(currentLine.lineNumber());
                ctx.appendChildIndent(indentFromColumn(start));
                ctx.setNotFinished(true);

                currentLine.skip();
            }

            return BlockState::Continue;
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState FencedCodeParser::continueCheck(Line &currentLine,
                                           TextStream &,
                                           QSharedPointer<Document>,
                                           Context &ctx,
                                           const QString &,
                                           const QString &)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (currentLine.column() >= ctx.indentColumn() || isEmptyLine(currentLine)) {
        qsizetype closeCount = 0;
        QChar fc;
        if (ctx.isInIndent(currentLine.column())
            && isCodeFences(currentLine, closeCount, fc, true)
            && fc == m_startChar
            && closeCount >= m_openCount) {
            currentLine.skip();

            ctx.setNotFinished(false);

            return BlockState::Stop;
        } else {
            currentLine.skip();

            return BlockState::Continue;
        }
    }

    ctx.setNotFinished(false);

    currentLine.restoreState();

    return BlockState::Stop;
}

inline qsizetype spacesCount(qsizetype column,
                             qsizetype indent)
{
    return (column - indent);
}

BlockState FencedCodeParser::process(Line &currentLine,
                                     TextStream &stream,
                                     QSharedPointer<Document>,
                                     QSharedPointer<Block> parent,
                                     Context &ctx,
                                     const QString &,
                                     const QString &,
                                     QStringList &)
{
    currentLine.saveState();

    if (m_startDelim.isNullPositions()) {
        if (isEmptyLine(currentLine)) {
            currentLine.skip();

            return BlockState::Continue;
        }

        skipSpaces(currentLine);

        ctx.setNotFinished(true);

        QString syntax;
        WithPosition syntaxPos;

        const auto pStartPos = currentLine.position();
        ctx.appendChildIndent(pStartPos);

        m_code = QSharedPointer<Code>::create(QString(), true, false);
        ctx.setItem(m_code.get());

        if (stream.atEnd()) {
            m_code->setStartColumn(currentLine.length());
            m_code->setStartLine(currentLine.lineNumber());
            m_code->setEndColumn(currentLine.length());
            m_code->setEndLine(currentLine.lineNumber());
        }

        m_startChar = currentLine.currentChar();
        m_startColumn = currentLine.column();

        parseStartOfCode(currentLine, &syntax, &m_startDelim, &syntaxPos);

        m_openCount = m_startDelim.endColumn() - m_startDelim.startColumn() + 1;

        m_emptyColumn = currentLine.length();
        m_emptyColumnLine = currentLine.lineNumber();

        if (syntax.toLower() == s_mathString) {
            m_code.reset();

            m_mathParagraph = QSharedPointer<Paragraph>::create();
            m_mathParagraph->setStartColumn(pStartPos);
            m_mathParagraph->setStartLine(currentLine.lineNumber());

            m_math = QSharedPointer<Math>::create();

            m_math->setInline(false);
            m_math->setStartDelim(m_startDelim);
            m_math->setSyntaxPos(syntaxPos);
            m_math->setFensedCode(true);

            if (stream.atEnd()) {
                m_math->setStartColumn(m_emptyColumn);
                m_math->setStartLine(m_emptyColumnLine);
                m_math->setEndColumn(m_emptyColumn);
                m_math->setEndLine(m_emptyColumnLine);
            }

            m_mathParagraph->appendItem(m_math);

            parent->appendItem(m_mathParagraph);
        } else {
            m_code->setStartDelim(m_startDelim);
            m_code->setSyntax(syntax);
            m_code->setSyntaxPos(syntaxPos);

            parent->appendItem(m_code);
        }

        currentLine.skip();

        return BlockState::Continue;
    } else {
        while (currentLine.currentChar().isSpace()
               && currentLine.position() < currentLine.length()
               && currentLine.column() < std::max(m_startColumn, indentFromColumn(ctx.indentColumn()))) {
            currentLine.nextChar();
        }

        if (currentLine.column() >= ctx.indentColumn() || isEmptyLine(currentLine)) {
            qsizetype closeCount = 0;
            QChar fc;

            qsizetype startPos = -1;

            {
                const auto rollback = currentLine.makeRollbackObject();
                skipSpaces(currentLine);
                startPos = currentLine.column();
            }

            if (isCodeFences(currentLine, closeCount, fc, true)
                && ctx.isInIndent(startPos)
                && fc == m_startChar
                && closeCount >= m_openCount) {
                if (m_code) {
                    m_code->setEndDelim({currentLine.position(),
                                         currentLine.lineNumber(),
                                         currentLine.length() - 1,
                                         currentLine.lineNumber()});

                    if (m_code->text().isEmpty()) {
                        m_code->setStartColumn(m_emptyColumn);
                        m_code->setStartLine(m_emptyColumnLine);
                        m_code->setEndColumn(m_emptyColumn);
                        m_code->setEndLine(m_emptyColumnLine);
                    }
                } else if (m_math) {
                    m_mathParagraph->setEndColumn(currentLine.length() - 1);
                    m_mathParagraph->setEndLine(currentLine.lineNumber());
                    m_math->setEndDelim({currentLine.position(),
                                         currentLine.lineNumber(),
                                         currentLine.length() - 1,
                                         currentLine.lineNumber()});

                    if (m_math->text().isEmpty()) {
                        m_math->setStartColumn(m_emptyColumn);
                        m_math->setStartLine(m_emptyColumnLine);
                        m_math->setEndColumn(m_emptyColumn);
                        m_math->setEndLine(m_emptyColumnLine);
                    }
                }

                currentLine.skip();

                reset(ctx);

                return BlockState::Stop;
            } else {
                if (m_math) {
                    if (!m_math->text().isEmpty()) {
                        m_math->setText(m_math->text() + s_newLineChar);
                    }

                    m_math->setText(
                        m_math->text()
                        + QString(spacesCount(currentLine.column(),
                                              std::max(ctx.indentColumn() ? ctx.indentColumn() + 1 : 0, m_startColumn)),
                                  s_spaceChar)
                        + currentLine.slicedCopy(currentLine.position()));

                    if (m_math->isNullPositions()) {
                        m_math->setStartColumn(ctx.indentColumn());
                        m_math->setStartLine(currentLine.lineNumber());
                        m_math->setEndColumn(currentLine.length() - 1);
                        m_math->setEndLine(currentLine.lineNumber());
                    } else {
                        m_math->setEndColumn(currentLine.length() - 1);
                        m_math->setEndLine(currentLine.lineNumber());
                    }
                } else {
                    if (m_code->isNullPositions()) {
                        m_code->setStartColumn(currentLine.position() - (currentLine.column() > m_startColumn ? 1 : 0));
                        m_code->setStartLine(currentLine.lineNumber());
                    }

                    QString data =
                        QString(spacesCount(currentLine.column(),
                                            std::max(ctx.indentColumn() ? ctx.indentColumn() + 1 : 0, m_startColumn)),
                                s_spaceChar)
                        + currentLine.slicedCopy(currentLine.position());

                    m_code->setEndColumn(currentLine.length()
                                             ? (currentLine.position() < currentLine.length() ? currentLine.length() - 1
                                                                                              : currentLine.position())
                                             : 0);
                    m_code->setEndLine(currentLine.lineNumber());

                    if (!data.isEmpty()) {
                        if (!m_code->text().isEmpty()) {
                            m_code->setText(m_code->text() + s_newLineChar);
                        }

                        for (qsizetype i = 0; i < m_emptyLinesCount; ++i) {
                            data.prepend(s_newLineChar);
                        }

                        m_emptyLinesCount = 0;

                        m_code->setText(m_code->text() + data);
                    } else {
                        ++m_emptyLinesCount;
                    }
                }

                currentLine.skip();

                return BlockState::Continue;
            }
        } else {
            currentLine.restoreState();

            reset(ctx);

            return BlockState::Stop;
        }
    }

    return BlockState::None;
}

void FencedCodeParser::reset(Context &ctx)
{
    ctx.setNotFinished(false);

    resetOnAllContexts();
}

void FencedCodeParser::resetOnAllContexts()
{
    if (m_code && m_emptyLinesCount) {
        QString data;

        for (qsizetype i = 0; i < m_emptyLinesCount; ++i) {
            data.append(s_newLineChar);
        }

        m_code->setText(m_code->text() + data);
    }

    m_startChar = {};
    m_code.reset();
    m_math.reset();
    m_mathParagraph.reset();
    m_startDelim = {};
    m_openCount = 0;
    m_emptyLinesCount = 0;
}

bool FencedCodeParser::mayBreakParagraph(Line &,
                                         TextStream &,
                                         QSharedPointer<Document>,
                                         Context &) const
{
    return true;
}

void FencedCodeParser::finish(Line &currentLine,
                              TextStream &stream,
                              QSharedPointer<Document>,
                              QSharedPointer<Block>,
                              Context &,
                              const QString &,
                              const QString &,
                              QStringList &)
{
    if (m_code && m_code->isNullPositions()) {
        const auto st = stream.currentState();
        const auto line = stream.moveTo(currentLine.lineNumber() - 1);

        m_code->setStartColumn(line.length());
        m_code->setStartLine(m_code->startDelim().startLine());
        m_code->setEndColumn(line.length());
        m_code->setEndLine(m_code->startLine());

        stream.restoreState(&st);
    }
}

} /* namespace MD */
