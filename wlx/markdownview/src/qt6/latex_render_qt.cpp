// LaTeX math rendering for the Qt6 plugin: MicroTeX's Qt Graphics2D
// backend. Only compiled into markdownview_qt6, which already links
// Qt6::Gui for its own UI -- this costs it nothing extra. See
// src/gtk3/latex_render_cairo.cpp for the GTK3 target's equivalent (Cairo
// backend, no Qt).

#include "core/latex_render.h"

#include "latex.h"
#include "platform/qt/graphic_qt.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QBuffer>
#include <QIODevice>

std::vector<uint8_t> renderLatexToPng(const std::string &tex, bool darkMode,
                                       int &logicalWidth, int &logicalHeight)
{
    // QPixmap/QPainter need a live QGuiApplication; the Qt6 plugin always
    // has one running (it's a Qt Lister plugin, hosted inside DC's Qt
    // event loop), so this is just a defensive guard, not an expected path.
    if (!qApp) return {};

    std::wstring wtex(tex.begin(), tex.end()); // ASCII/Latin-1-range LaTeX source; matches prior toStdWString() usage

    constexpr int oversample = 8;
    constexpr float baseTextSize = 20.0f;
    constexpr float renderTextSize = baseTextSize * oversample;
    unsigned int fgColor = darkMode ? 0xfff0f6fc : 0xff000000;

    tex::TeXRender *render = nullptr;
    try {
        render = tex::LaTeX::parse(wtex, 0, renderTextSize, renderTextSize, fgColor);
    } catch (const std::exception &) {
        return {};
    }
    if (!render || render->getWidth() <= 0 || render->getHeight() <= 0) {
        delete render;
        return {};
    }

    int padding = 4 * oversample;
    int physicalWidth = std::max(1, render->getWidth() + padding * 2);
    int physicalHeight = std::max(1, render->getHeight() + render->getDepth() + padding * 2);

    QPixmap pixmap(physicalWidth, physicalHeight);
    QColor bgColor = darkMode ? QColor(QStringLiteral("#0d1117")) : QColor(QStringLiteral("#FAFAFA"));
    pixmap.fill(bgColor);

    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        tex::Graphics2D_qt g2(&painter);
        render->draw(g2, padding, padding);
    }
    delete render;

    QImage image = pixmap.toImage();
    logicalWidth = physicalWidth / oversample;
    logicalHeight = physicalHeight / oversample;
    QImage scaledImage = image.scaled(logicalWidth, logicalHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    scaledImage.save(&buffer, "PNG");

    return std::vector<uint8_t>(ba.begin(), ba.end());
}
