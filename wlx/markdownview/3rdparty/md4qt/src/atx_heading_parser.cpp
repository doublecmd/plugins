/*
    SPDX-FileCopyrightText: 2026 Igor Mironchik <igor.mironchik@gmail.com>
    SPDX-License-Identifier: MIT
*/

// md4qt include.
#include "atx_heading_parser.h"
#include "constants.h"
#include "emphasis_parser.h"
#include "inline_context.h"
#include "paragraph_parser.h"
#include "parser.h"
#include "reverse_solidus.h"
#include "utils.h"

namespace MD
{

ATXHeadingParser::ATXHeadingParser(Parser *parser)
    : BlockParser(parser)
{
}

ATXHeadingParser::~ATXHeadingParser() = default;

BlockState ATXHeadingParser::check(Line &currentLine,
                                   TextStream &,
                                   QSharedPointer<Document>,
                                   Context &ctx,
                                   const QString &,
                                   const QString &,
                                   bool checkWithoutProcessing)
{
    currentLine.saveState();

    skipSpaces(currentLine);

    if (ctx.isInIndent(currentLine.column())) {
        int count = 0;

        while (currentLine.currentChar() == s_numberSignChar) {
            ++count;
            currentLine.nextChar();
        }

        if (count && count <= 6 && (currentLine.currentChar().isSpace() || isEmptyLine(currentLine))) {
            if (checkWithoutProcessing) {
                currentLine.restoreState();
            } else {
                currentLine.skip();
            }

            return BlockState::Stop;
        }
    }

    currentLine.restoreState();

    return BlockState::None;
}

BlockState ATXHeadingParser::continueCheck(Line &,
                                           TextStream &,
                                           QSharedPointer<Document>,
                                           Context &,
                                           const QString &,
                                           const QString &)
{
    return BlockState::Stop;
}

inline QPair<QString,
             WithPosition>
findHeaderLabel(const QString &s)
{
    const auto start = s.indexOf(s_startIdString);

    if (start >= 0) {
        const auto p = s.indexOf(s_rightCurlyBracketChar, start + 2);

        if (p >= start + 2) {
            WithPosition pos;
            pos.setStartColumn(start);
            pos.setEndColumn(p);

            const auto label = s.sliced(start, p - start + 1);

            return {label, pos};
        }
    }

    return {};
}

inline WithPosition findAndRemoveClosingSequence(QString &s)
{
    qsizetype end = -1;
    qsizetype start = -1;

    for (long long int i = s.length() - 1; i >= 0; --i) {
        if (!s[i].isSpace() && s[i] != s_numberSignChar && end == -1) {
            return {};
        }

        if (s[i] == s_numberSignChar) {
            if (end == -1) {
                end = i;
            }

            if (i - 1 >= 0) {
                if (s[i - 1].isSpace()) {
                    start = i;
                    break;
                } else if (s[i - 1] != s_numberSignChar) {
                    return {};
                }
            } else {
                start = 0;
            }
        }
    }

    WithPosition ret;

    if (start != -1 && end != -1) {
        ret.setStartColumn(start);
        ret.setEndColumn(end);

        s.remove(start, end - start + 1);
    }

    return ret;
}

void ATXHeadingParser::processLabel(QSharedPointer<Paragraph> paragraph,
                                    const QString &path,
                                    const QString &fileName,
                                    QSharedPointer<Heading> heading,
                                    QSharedPointer<Document> doc)
{
    QString label = s_numberSignChar + ParagraphParser::paragraphToLabel(paragraph);

    const QString labelPath = s_solidusChar + (!path.isEmpty() ? QString(path + s_solidusChar) : QString()) + fileName;

    if (doc->auxLabelsMap().contains(label)) {
        if (doc->auxLabelsMap()[label].contains(labelPath)) {
            const auto count = doc->auxLabelsMap()[label][labelPath];
            doc->auxLabelsMap()[label][labelPath] = count + 1;
            label.append(s_minusChar + QString::number(count + 1));
            doc->auxLabelsMap().insert(label, {});
            doc->auxLabelsMap()[label].insert(labelPath, 0);
        } else {
            doc->auxLabelsMap()[label].insert(labelPath, 0);
        }
    } else {
        doc->auxLabelsMap().insert(label, {});
        doc->auxLabelsMap()[label].insert(labelPath, 0);
    }

    heading->setLabel(label + labelPath);
    heading->labelVariants().push_back(heading->label());

    doc->insertLabeledHeading(label + labelPath, heading);

    if (label != label.toLower()) {
        doc->insertLabeledHeading(label.toLower() + labelPath, heading);
        heading->labelVariants().push_back(label.toLower() + labelPath);
    }
}

BlockState ATXHeadingParser::process(Line &currentLine,
                                     TextStream &,
                                     QSharedPointer<Document> doc,
                                     QSharedPointer<Block> parent,
                                     Context &ctx,
                                     const QString &path,
                                     const QString &fileName,
                                     QStringList &linksToParse)
{
    if (isEmptyLine(currentLine)) {
        currentLine.skip();

        return BlockState::Continue;
    }

    skipSpaces(currentLine);

    auto heading = QSharedPointer<Heading>::create();
    heading->setStartColumn(currentLine.position());
    heading->setStartLine(currentLine.lineNumber());
    heading->setEndColumn(currentLine.length() - 1);
    heading->setEndLine(currentLine.lineNumber());
    parent->appendItem(heading);
    ctx.setItem(heading.get());

    Heading::Delims delims;
    WithPosition startDelim;
    startDelim.setStartColumn(currentLine.position());
    startDelim.setStartLine(currentLine.lineNumber());

    int count = 0;

    while (currentLine.currentChar() == s_numberSignChar) {
        ++count;
        currentLine.nextChar();
    }

    startDelim.setEndColumn(currentLine.position() - 1);
    startDelim.setEndLine(currentLine.lineNumber());
    delims.append(startDelim);

    heading->setLevel(count);

    currentLine.nextChar();

    auto s = QString(currentLine.view());
    auto endDelim = findAndRemoveClosingSequence(s);
    endDelim.setStartLine(currentLine.lineNumber());
    endDelim.setEndLine(currentLine.lineNumber());

    if (endDelim.startColumn() != -1) {
        delims.append(endDelim);
    }

    heading->setDelims(delims);

    auto line = Line(s, currentLine.lineNumber());
    const auto st = currentLine.currentState();
    line.restoreState(&st);

    auto paragraph = QSharedPointer<Paragraph>::create();
    paragraph->setStartColumn(line.position());
    paragraph->setStartLine(line.lineNumber());

    paragraph->setEndColumn(line.length() - 1);
    paragraph->setEndLine(line.lineNumber());

    InlineContext inlineContext;

    auto label = findHeaderLabel(s);

    if (!label.first.isEmpty()) {
        heading->setLabel(label.first.sliced(1, label.first.length() - 2)
                          + s_solidusChar
                          + (!path.isEmpty() ? QString(path + s_solidusChar) : QString())
                          + fileName);

        label.second.setStartLine(currentLine.lineNumber());
        label.second.setEndLine(currentLine.lineNumber());

        heading->setLabelPos(label.second);
    }

    line.saveState();

    parser()->pushStateOfInliners();

    ParagraphStream::HashedLines lines;
    lines.insert(line.lineNumber(), line);

    ParagraphStream pStream(lines, line.lineNumber(), line.lineNumber());
    const auto pst = pStream.currentState();

    line = pStream.readLine();

    ReverseSolidusHandler rs;

    while (line.position() < line.length()) {
        auto processed = false;

        rs.process(line.currentChar());

        if (!(line.position() >= label.second.startColumn() && line.position() <= label.second.endColumn())) {
            const auto parsers = parser()->inlineParsersFor(line.currentChar());

            for (const auto &p : parsers) {
                if (p->check(line, pStream, inlineContext, doc, path, fileName, linksToParse, *parser(), rs)) {
                    processed = true;
                    break;
                }
            }
        }

        if (!processed) {
            rs.next();
            line.nextChar();
        } else {
            rs.clear();
        }
    }

    pStream.restoreState(&pst);

    parser()->popStateOfInliners();

    EmphasisParser::processEmphasises(inlineContext);
    ParagraphParser::makeTextObjects(inlineContext, pStream, paragraph, label.second);

    heading->setText(paragraph);

    if (heading->isLabeled()) {
        doc->insertLabeledHeading(heading->label(), heading);
        heading->labelVariants().push_back(heading->label());
    } else {
        processLabel(paragraph, path, fileName, heading, doc);
    }

    currentLine.skip();

    return BlockState::Stop;
}

void ATXHeadingParser::reset(Context &)
{
}

void ATXHeadingParser::resetOnAllContexts()
{
}

bool ATXHeadingParser::mayBreakParagraph(Line &,
                                         TextStream &,
                                         QSharedPointer<Document>,
                                         Context &) const
{
    return true;
}

} /* namespace MD */
