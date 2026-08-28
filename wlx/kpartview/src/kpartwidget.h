#ifndef KPARTWIDGET_H
#define KPARTWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <KParts/ReadOnlyPart>
#include <KPluginMetaData>
#include <QUrl>
#include <QPointer>

class KPartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KPartWidget(QWidget *parent = nullptr);
    ~KPartWidget();

    bool loadFile(const QString &fileName);
    void setActive(bool active);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void installFocusGuard();
    void returnFocusToDC();
    void restoreFocusToDC();
    void instantiatePart();
    void restoreZoom();
    bool scrollView(int key);

    KParts::ReadOnlyPart *m_part;
    QVBoxLayout *m_layout;
    int m_loadGeneration;
    QUrl m_pendingUrl;
    KPluginMetaData m_selectedPart;
    QPointer<QWidget> m_savedFocusWidget;
    QPointer<QWidget> m_partFocusWidget;
    bool m_isActive = false;
    bool m_needZoomRestore = false;
};

#endif // KPARTWIDGET_H
