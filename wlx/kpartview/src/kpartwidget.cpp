#include "kpartwidget.h"
#include <QMimeDatabase>
#include <KParts/ReadOnlyPart>
#include <KParts/PartLoader>
#include <KParts/OpenUrlArguments>
#include <KPluginMetaData>
#include <QUrl>
#include <QEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QChildEvent>
#include <QResizeEvent>
#include <QEnterEvent>
#include <QScrollBar>
#include <QAbstractScrollArea>
#include <QMessageBox>
#include <QDialog>
#include <QAction>
#include <QSettings>
#include <QDir>
#include <QDebug>
#include <KActionCollection>
#include <KXMLGUIFactory>

KPartWidget::KPartWidget(QWidget *parent)
    : QWidget(parent)
    , m_part(nullptr)
    , m_loadGeneration(0)
    , m_needZoomRestore(false)
{
    // NoFocus by default — activation is managed exclusively via
    // lc_focus from DC and geometry-based click detection.
    setFocusPolicy(Qt::NoFocus);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Install global event filter to intercept focus-stealing by KParts
    // at the application level, regardless of which widget they target.
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }

    // Connect to focusChanged to bounce focus back when inactive.
    // This is the primary defence against KParts stealing focus
    // programmatically (e.g. Okular after rendering a page).
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
        if (!m_part || !m_part->widget()) return;

        bool nowInside = now && (now == this || this->isAncestorOf(now));
        bool isPopup = now && ((now->windowFlags() & Qt::Popup) || (now->windowFlags() & Qt::ToolTip) || now->inherits("QMenu"));

        if (m_isActive) {
            // If focus left the plugin while active, deactivate.
            bool oldInside = old && (old == this || this->isAncestorOf(old));
            if (oldInside && !nowInside && !isPopup) {
                setActive(false);
            }
        } else {
            // Inactive: if focus entered the plugin, bounce it back.
            if (nowInside) {
                // Learn which widget the KPart naturally wants to focus.
                if (now != this) {
                    m_partFocusWidget = now;
                }
                QPointer<QWidget> pOld(old);
                QTimer::singleShot(0, this, [this, pOld]() {
                    QWidget *currentFocus = QApplication::focusWidget();
                    bool stillInside = currentFocus &&
                        (currentFocus == this || this->isAncestorOf(currentFocus));
                    if (stillInside) {
                        if (pOld) {
                            pOld->setFocus(Qt::OtherFocusReason);
                        } else {
                            restoreFocusToDC();
                        }
                    }
                });
            }
        }
    });
}

KPartWidget::~KPartWidget()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeEventFilter(this);
    }
    if (m_part) {
        m_part->closeUrl();
        delete m_part;
    }
}

void KPartWidget::returnFocusToDC()
{
    setActive(false);
}

void KPartWidget::restoreFocusToDC()
{
    if (m_savedFocusWidget) {
        m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
    } else if (this->parentWidget()) {
        this->parentWidget()->setFocus(Qt::OtherFocusReason);
    }
}

void KPartWidget::setActive(bool active)
{
    if (m_isActive == active)
        return;

    m_isActive = active;

    if (!active) {
        this->clearFocus();
        if (m_part && m_part->widget()) {
            m_part->widget()->clearFocus();
        }
        if (this->parentWidget()) {
            this->parentWidget()->setFocus(Qt::OtherFocusReason);
        }
    } else {
        // Find the best widget to give focus to:
        // 1. m_partFocusWidget (learned from the KPart's own focus steal)
        // 2. Walk the focus proxy chain from m_part->widget()
        // 3. Search for a child with StrongFocus policy
        // 4. Fall back to m_part->widget() itself
        QWidget *target = nullptr;
        if (m_part && m_part->widget()) {
            if (m_partFocusWidget) {
                target = m_partFocusWidget.data();
            } else {
                // Walk focus proxy chain
                target = m_part->widget();
                while (target->focusProxy()) {
                    target = target->focusProxy();
                }
                // If proxy chain led nowhere useful, search children
                if (target == m_part->widget() &&
                    !(target->focusPolicy() & Qt::StrongFocus)) {
                    for (QWidget *child : m_part->widget()->findChildren<QWidget*>()) {
                        if (child->focusPolicy() & Qt::StrongFocus) {
                            target = child;
                            break;
                        }
                    }
                }
            }
            target->setFocus(Qt::OtherFocusReason);
        }
    }
}

void KPartWidget::installFocusGuard()
{
    if (!m_part || !m_part->widget()) return;

    // Install event filters to intercept mouse/key/focus events, but do NOT
    // override focus policies.  The focusChanged bounce-back (see constructor)
    // is the sole defence against programmatic focus steals.  Keeping the
    // native focus policies lets KPart widgets actually receive keyboard
    // input (arrows, pgup/down, home/end) when the plugin is active.
    m_part->widget()->installEventFilter(this);
    m_part->widget()->setAttribute(Qt::WA_NativeWindow, false);

    for (QWidget *child : m_part->widget()->findChildren<QWidget*>()) {
        child->setAttribute(Qt::WA_NativeWindow, false);
        child->installEventFilter(this);
    }
}

bool KPartWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (m_part && watched == m_part->widget() && m_needZoomRestore) {
        if (event->type() == QEvent::Paint || event->type() == QEvent::Resize) {
            m_needZoomRestore = false;
            QTimer::singleShot(0, this, [this]() {
                restoreZoom();
            });
        }
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        // Geometry-based click detection, mirroring FocusManager pattern.
        if (!m_part || !m_part->widget()) break;

        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        const QRect gr(mapToGlobal(QPoint(0, 0)), size());

        if (m_isActive && !gr.contains(gp)) {
            setActive(false);
        } else if (!m_isActive && gr.contains(gp)) {
            // Following FocusManager pattern: just set the flag.
            // Let the click event propagate naturally so Qt gives
            // click-focus to the widget the user actually clicked.
            m_isActive = true;
        }
        break;
    }
    case QEvent::KeyPress: {
        if (m_isActive) {
            QKeyEvent *ke = static_cast<QKeyEvent*>(event);
            // Ctrl+Q: deactivate and forward to DC.
            if (ke->key() == Qt::Key_Q && (ke->modifiers() & Qt::ControlModifier)) {
                setActive(false);
                QTimer::singleShot(0, this, [this]() {
                    QWidget *target = QApplication::activeWindow();
                    if (!target) target = this->window();
                    if (target) {
                        QCoreApplication::postEvent(target,
                            new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier));
                        QCoreApplication::postEvent(target,
                            new QKeyEvent(QEvent::KeyRelease, Qt::Key_Q, Qt::ControlModifier));
                    }
                });
                return true;
            }

            // Manual Shortcut Routing for Zoom Actions
            if (m_part) {
                QString actionToTrigger;
                if (ke->key() == Qt::Key_F && ke->modifiers() == Qt::NoModifier) {
                    actionToTrigger = QStringLiteral("view_zoom_to_fit");
                } else if (ke->key() == Qt::Key_0 && (ke->modifiers() & Qt::ControlModifier)) {
                    actionToTrigger = QStringLiteral("view_actual_size");
                } else if (ke->key() == Qt::Key_Plus || ke->key() == Qt::Key_Equal) {
                    actionToTrigger = QStringLiteral("view_zoom_in");
                } else if (ke->key() == Qt::Key_Minus) {
                    actionToTrigger = QStringLiteral("view_zoom_out");
                } else if (ke->key() == Qt::Key_S && (ke->modifiers() & Qt::ControlModifier) && (ke->modifiers() & Qt::ShiftModifier)) {
                    actionToTrigger = QStringLiteral("file_save_as");
                }

                if (!actionToTrigger.isEmpty()) {
                    // Try actionCollection first
                    QAction *act = m_part->actionCollection()
                                       ? m_part->actionCollection()->action(actionToTrigger)
                                       : nullptr;
                    // Fallback: findChild by objectName
                    if (!act) {
                        act = m_part->findChild<QAction*>(actionToTrigger, Qt::FindChildrenRecursively);
                    }
                    qDebug() << "[KPartWidget] KeyPress: key=" << ke->key()
                             << "looking for action:" << actionToTrigger
                             << "found:" << (act != nullptr)
                             << "enabled:" << (act ? act->isEnabled() : false)
                             << "m_isActive:" << m_isActive;
                    if (act && act->isEnabled()) {
                        act->trigger();
                        return true;
                    } else if (!act) {
                        // Dump all available actions for diagnosis
                        qDebug() << "[KPartWidget] Action not found. Available actions from actionCollection:";
                        if (m_part->actionCollection()) {
                            const auto allActions = m_part->actionCollection()->actions();
                            for (const QAction *a : allActions) {
                                qDebug() << "  -" << a->objectName() << "text:" << a->text()
                                         << "checkable:" << a->isCheckable()
                                         << "checked:" << a->isChecked()
                                         << "enabled:" << a->isEnabled();
                            }
                        }
                        qDebug() << "[KPartWidget] Available actions from findChildren:";
                        const auto childActions = m_part->findChildren<QAction*>(Qt::FindChildrenRecursively);
                        for (const QAction *a : childActions) {
                            qDebug() << "  -" << a->objectName() << "text:" << a->text()
                                     << "checkable:" << a->isCheckable()
                                     << "checked:" << a->isChecked()
                                     << "enabled:" << a->isEnabled();
                        }
                    }
                }
            }

            // Navigation keys: if the event has propagated up to KPartWidget,
            // it means the KPart didn't handle it (e.g. KTextEditor in
            // read-only embedded mode). Scroll the view ourselves.
            if (watched == this) {
                switch (ke->key()) {
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_PageUp:
                case Qt::Key_PageDown:
                case Qt::Key_Home:
                case Qt::Key_End:
                    if (scrollView(ke->key())) {
                        return true;
                    }
                    break;
                }
            }
        }
        break;
    }
    case QEvent::ChildAdded: {
        // Okular/Calligra spawn widgets asynchronously (e.g. PageView).
        // Apply focus guards to each new child in our KPart's subtree.
        QChildEvent *ce = static_cast<QChildEvent*>(event);
        if (ce->child() && ce->child()->isWidgetType()) {
            QWidget *childWidget = static_cast<QWidget*>(ce->child());
            if (m_part && m_part->widget() &&
                (watched == m_part->widget() || m_part->widget()->isAncestorOf(static_cast<QWidget*>(watched)))) {
                childWidget->setAttribute(Qt::WA_NativeWindow, false);
                childWidget->installEventFilter(this);
            }
        }
        break;
    }

    case QEvent::FocusIn: {
        // When inactive, block any programmatic focus entry into the
        // plugin subtree. The focusChanged connection above handles the
        // bounce, but this provides belt-and-suspenders protection.
        if (!m_isActive && watched->isWidgetType()) {
            QWidget *w = static_cast<QWidget*>(watched);
            bool isOurs = (w == this);
            if (!isOurs && m_part && m_part->widget()) {
                isOurs = (w == m_part->widget() || m_part->widget()->isAncestorOf(w));
            }
            if (isOurs) {
                QFocusEvent *fe = static_cast<QFocusEvent*>(event);
                if (fe->reason() == Qt::OtherFocusReason ||
                    fe->reason() == Qt::ActiveWindowFocusReason) {
                    // Programmatic focus steal while inactive — bounce it
                    QTimer::singleShot(0, this, [this]() {
                        restoreFocusToDC();
                    });
                }
            }
        }
        break;
    }

    case QEvent::Show: {
        if (watched->isWidgetType() && watched->inherits("QMessageBox")) {
            QMessageBox *mb = qobject_cast<QMessageBox*>(watched);
            if (mb) {
                // If a KPart pops up a warning about a file being deleted or modified,
                // reject it automatically to prevent freezing Double Commander.
                QString text = mb->text();
                if (text.contains(QLatin1String("deleted"), Qt::CaseInsensitive) || 
                    text.contains(QLatin1String("modified"), Qt::CaseInsensitive)) {
                    QTimer::singleShot(0, mb, &QDialog::reject);
                }
            }
        }
        break;
    }

    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

bool KPartWidget::loadFile(const QString &fileName)
{
    QUrl url = QUrl::fromLocalFile(fileName);

    if (m_part && m_pendingUrl == url) {
        // Double Commander is asking us to reload the same file (e.g., it was modified).
        // Many KParts (like Gwenview) auto-reload the file internally.
        // Destroying the KPart here while it's processing its own file watcher events
        // causes severe GLib/GTK crashes. We just safely re-open the URL.
        KParts::OpenUrlArguments args;
        args.setReload(true);
        m_part->setArguments(args);
        m_part->openUrl(url);
        return true;
    }

    // Save which widget currently has focus (DC's file list) so we can
    // restore it after the KPart inevitably steals focus.
    m_savedFocusWidget = QApplication::focusWidget();

    // Increment generation to invalidate any queued callbacks from the
    // previous part before we tear it down.
    m_loadGeneration++;

    if (m_part) {
        // Deactivate but don't try to restore focus yet — we're about to
        // tear down and rebuild the part.
        m_isActive = false;
        m_partFocusWidget = nullptr;

        m_part->closeUrl();
        QWidget *oldWidget = m_part->widget();
        if (oldWidget) {
            m_layout->removeWidget(oldWidget);
            oldWidget->hide();
            oldWidget->setParent(nullptr);
        }
        m_part->deleteLater();
        m_part = nullptr;
    }

    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fileName);

    // If the file is detected as a generic ZIP but has a more specific extension
    // (like .docx, .odt, etc.), prioritize the extension-based MIME type.
    if (mime.name() == QLatin1String("application/zip") || mime.isDefault()) {
        QMimeType extMime = db.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
        if (!extMime.isDefault() && extMime.name() != mime.name()) {
            mime = extMime;
        }
    }

    // Find all parts available for this MIME type
    QVector<KPluginMetaData> parts = KParts::PartLoader::partsForMimeType(mime.name());

    KPluginMetaData selectedPart;

    // First pass: look for specialized renderers (not archives, not terminal)
    for (const auto &metaData : parts) {
        QString pluginId = metaData.pluginId();
        if (pluginId.contains(QLatin1String("konsole"), Qt::CaseInsensitive) ||
            pluginId.contains(QLatin1String("arkpart"), Qt::CaseInsensitive) ||
            pluginId.contains(QLatin1String("kioarchive"), Qt::CaseInsensitive)) {
            continue;
        }
        selectedPart = metaData;
        break;
    }

    // Second pass: if no specialized renderer found, allow archive explorers as fallback
    if (!selectedPart.isValid()) {
        for (const auto &metaData : parts) {
            QString pluginId = metaData.pluginId();
            if (pluginId.contains(QLatin1String("konsole"), Qt::CaseInsensitive)) {
                continue;
            }
            if (pluginId.contains(QLatin1String("arkpart"), Qt::CaseInsensitive) ||
                pluginId.contains(QLatin1String("kioarchive"), Qt::CaseInsensitive)) {
                selectedPart = metaData;
                break;
            }
        }
    }

    if (selectedPart.isValid()) {
        m_pendingUrl = url;
        m_selectedPart = selectedPart;

        // Defer instantiation by 50ms so Double Commander can finish handling
        // the user's MouseRelease event on the file list. Without this delay,
        // complex KParts spin up Wayland grabs so fast that DC misses the
        // release and gets stuck in a phantom-drag mode.
        QTimer::singleShot(50, this, [this, gen = m_loadGeneration]() {
            if (gen == m_loadGeneration) {
                instantiatePart();
            }
        });

        return true;
    }

    return false;
}

void KPartWidget::instantiatePart()
{
    // If the selected part is Gwenview, temporarily redirect XDG_CONFIG_HOME
    // to a custom isolated folder so we can supply our own gwenviewrc (with EnlargeSmallerImages=true)
    // without ever touching or risking corruption of the user's main ~/.config/gwenviewrc.
    QByteArray originalXdgConfig;
    bool redirectedXdg = false;
    
    if (m_selectedPart.pluginId() == QLatin1String("gvpart")) {
        originalXdgConfig = qgetenv("XDG_CONFIG_HOME");
        
        QString customXdgPath = QDir::homePath() + QStringLiteral("/.config/doublecmd/kpart_gwenview_config");
        QDir().mkpath(customXdgPath);
        
        // Write the custom gwenviewrc configuration
        QString customRcPath = customXdgPath + QStringLiteral("/gwenviewrc");
        QSettings gwenviewSettings(customRcPath, QSettings::IniFormat);
        gwenviewSettings.beginGroup(QStringLiteral("ImageView"));
        gwenviewSettings.setValue(QStringLiteral("EnlargeSmallerImages"), true);
        gwenviewSettings.sync();
        
        qputenv("XDG_CONFIG_HOME", customXdgPath.toLocal8Bit());
        redirectedXdg = true;
        qDebug() << "[KPartWidget] Redirected XDG_CONFIG_HOME to" << customXdgPath;
    }

    auto result = KParts::PartLoader::instantiatePart<KParts::ReadOnlyPart>(m_selectedPart, this, this);
    
    // Restore the environment variable immediately after instantiation so it does
    // not affect the rest of Double Commander or other plugins.
    if (redirectedXdg) {
        if (originalXdgConfig.isEmpty()) {
            qunsetenv("XDG_CONFIG_HOME");
        } else {
            qputenv("XDG_CONFIG_HOME", originalXdgConfig);
        }
        qDebug() << "[KPartWidget] Restored original XDG_CONFIG_HOME";
    }

    if (result) {
        m_part = result.plugin;

        m_layout->addWidget(m_part->widget());

        installFocusGuard();
        
        // Lambda to configure actions: connect toggled signals, set shortcut contexts, etc.
        // Must be called AFTER the part is fully loaded (from completed signal).
        auto configureAndRestore = [this]() {
            if (!m_part || !m_part->widget()) return;
            
            qDebug() << "[KPartWidget] configureAndRestore: KPartWidget size=" << this->size()
                     << "m_part->widget() size=" << m_part->widget()->size();
            
            // Collect actions from both sources, deduplicating
            QSet<QAction*> seen;
            QList<QAction*> allActions;
            
            if (m_part->actionCollection()) {
                const auto acActions = m_part->actionCollection()->actions();
                qDebug() << "[KPartWidget] configureAndRestore: actionCollection has" << acActions.size() << "actions";
                for (QAction *a : acActions) {
                    if (!seen.contains(a)) {
                        seen.insert(a);
                        allActions.append(a);
                    }
                }
            }
            
            const auto childActions = m_part->findChildren<QAction*>(Qt::FindChildrenRecursively);
            qDebug() << "[KPartWidget] configureAndRestore: findChildren found" << childActions.size() << "actions";
            for (QAction *a : childActions) {
                if (!seen.contains(a)) {
                    seen.insert(a);
                    allActions.append(a);
                }
            }
            
            qDebug() << "[KPartWidget] configureAndRestore: total unique actions:" << allActions.size();
            
            for (QAction *act : allActions) {
                QString actionName = act->objectName();
                
                qDebug() << "[KPartWidget] Action:" << actionName
                         << "text:" << act->text()
                         << "checkable:" << act->isCheckable()
                         << "checked:" << act->isChecked()
                         << "enabled:" << act->isEnabled()
                         << "shortcutContext:" << act->shortcutContext()
                         << "shortcuts:" << act->shortcuts();
                
                // Add action to widget so shortcuts with WidgetWithChildrenShortcut can work
                if (!m_part->widget()->actions().contains(act)) {
                    m_part->widget()->addAction(act);
                }
                
                if (act->shortcutContext() == Qt::WindowShortcut || act->shortcutContext() == Qt::ApplicationShortcut) {
                    act->setShortcutContext(Qt::WidgetWithChildrenShortcut);
                }
                
                // Guard against duplicate connections using a dynamic property.
                // Qt::UniqueConnection does NOT work with lambdas.
                if (act->isCheckable() && !actionName.isEmpty()
                    && !act->property("_kpw_connected").toBool()) {
                    act->setProperty("_kpw_connected", true);
                    
                    connect(act, &QAction::toggled, this, [this, actionName](bool checked) {
                        qDebug() << "[KPartWidget] Action toggled:" << actionName << "checked:" << checked;
                        
                        bool isZoomAction = (actionName == QLatin1String("view_zoom_to_fit") ||
                                             actionName == QLatin1String("view_actual_size"));
                        
                        if (!isZoomAction || checked) {
                            QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("doublecmd"), QStringLiteral("kpartview"));
                            s.setValue(actionName, checked);
                            
                            if (isZoomAction && checked) {
                                QString otherName = (actionName == QLatin1String("view_zoom_to_fit"))
                                    ? QStringLiteral("view_actual_size")
                                    : QStringLiteral("view_zoom_to_fit");
                                s.setValue(otherName, false);
                            }
                            s.sync();
                            qDebug() << "[KPartWidget] Saved" << actionName << "=" << checked << "to" << s.fileName();
                        }
                        
                        if (m_part) {
                            // Radio-button behavior for zoom modes:
                            // checking one unchecks the other.
                            QString otherName;
                            if (actionName == QLatin1String("view_zoom_to_fit")) {
                                otherName = QStringLiteral("view_actual_size");
                            } else if (actionName == QLatin1String("view_actual_size")) {
                                otherName = QStringLiteral("view_zoom_to_fit");
                            }
                            if (!otherName.isEmpty()) {
                                QAction *other = m_part->actionCollection()
                                                     ? m_part->actionCollection()->action(otherName)
                                                     : nullptr;
                                if (!other) {
                                    other = m_part->findChild<QAction*>(otherName, Qt::FindChildrenRecursively);
                                }
                                if (checked && other && other->isChecked()) {
                                    qDebug() << "[KPartWidget] Radio: visually unchecking" << otherName;
                                    other->blockSignals(true);
                                    other->setChecked(false);
                                    other->blockSignals(false);
                                }
                            }
                        }
                        
                        if (m_part && m_part->widget()) {
                            QResizeEvent re(m_part->widget()->size(), m_part->widget()->size());
                            QCoreApplication::sendEvent(m_part->widget(), &re);
                            m_part->widget()->update();
                        }
                    });
                }
            }
            
            // Now restore saved settings (connections are already established above).
            // We must use trigger() instead of setChecked() so the KPart's internal
            // handler fires (e.g. Gwenview actually changes zoom mode, not just checkbox).
            QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("doublecmd"), QStringLiteral("kpartview"));
            qDebug() << "[KPartWidget] Restoring settings from" << settings.fileName() << "keys:" << settings.allKeys();
            
            restoreZoom();
            
            // Restore non-zoom checkable actions normally
            static const QString zoomFit = QStringLiteral("view_zoom_to_fit");
            static const QString zoomActual = QStringLiteral("view_actual_size");
            for (QAction *act : allActions) {
                if (!act->isCheckable()) continue;
                QString actionName = act->objectName();
                // Skip zoom pair (already handled above)
                if (actionName == zoomFit || actionName == zoomActual) continue;
                if (!actionName.isEmpty() && settings.contains(actionName)) {
                    bool desired = settings.value(actionName).toBool();
                    qDebug() << "[KPartWidget] Restoring" << actionName
                             << "current:" << act->isChecked() << "desired:" << desired;
                    if (desired && !act->isChecked()) {
                        act->trigger();
                    } else if (!desired && act->isChecked()) {
                        act->setChecked(false);
                    }
                }
            }
        };
        
        connect(m_part, &KParts::ReadOnlyPart::completed, this, [this, configureAndRestore]() {
            installFocusGuard();
            restoreFocusToDC();
            m_needZoomRestore = true;
            
            // Configure action connections now that the part is fully loaded.
            // The restore portion inside configureAndRestore uses trigger() which
            // must run AFTER all of Gwenview's own completed handlers have finished
            // (otherwise Gwenview resets zoom to its defaults after our restore).
            // QTimer::singleShot(0) defers to the next event loop iteration.
            QTimer::singleShot(0, this, [this, configureAndRestore]() {
                configureAndRestore();
            });
            
            if (m_part && m_part->widget()) {
                QTimer::singleShot(300, m_part->widget(), [this, w = m_part->widget()]() {
                    QCoreApplication::postEvent(w, new QEvent(QEvent::WindowActivate));
                    QCoreApplication::postEvent(w, new QResizeEvent(w->size(), w->size()));
                    QCoreApplication::postEvent(w, new QEnterEvent(QPointF(0,0), QPointF(0,0), QPointF(0,0)));
                    QCoreApplication::postEvent(w, new QEvent(QEvent::Leave));
                    w->update();
                    
                    for (QWidget *child : w->findChildren<QWidget*>()) {
                        QCoreApplication::postEvent(child, new QEvent(QEvent::WindowActivate));
                        QCoreApplication::postEvent(child, new QResizeEvent(child->size(), child->size()));
                        child->update();
                    }
                    
                    if (m_selectedPart.pluginId() == QLatin1String("markdownpart")) {
                        QScrollBar *vbar = nullptr;
                        QAbstractScrollArea *scrollArea = w->findChild<QAbstractScrollArea*>();
                        if (scrollArea) {
                            vbar = scrollArea->verticalScrollBar();
                        }
                        if (!vbar) {
                            for (QScrollBar *bar : w->findChildren<QScrollBar*>()) {
                                if (bar->orientation() == Qt::Vertical && bar->maximum() > 0) {
                                    vbar = bar;
                                    break;
                                }
                            }
                        }
                        if (vbar) {
                            vbar->setValue(0);
                        }
                    } else if (m_selectedPart.pluginId() == QLatin1String("okularpart")) {
                        // Enable text selection tool by default in Okular so users can copy text.
                        QAction *textSelect = m_part->actionCollection() ? m_part->actionCollection()->action(QStringLiteral("mouse_textselect")) : nullptr;
                        if (!textSelect) {
                            textSelect = m_part->findChild<QAction*>(QStringLiteral("mouse_textselect"), Qt::FindChildrenRecursively);
                        }
                        if (textSelect) {
                            textSelect->trigger();
                        }
                    }
                });
            }
        });
        connect(m_part, &KParts::ReadOnlyPart::completedWithPendingAction, this, [this, configureAndRestore]() {
            installFocusGuard();
            restoreFocusToDC();
            m_needZoomRestore = true;
            QTimer::singleShot(0, this, [this, configureAndRestore]() {
                configureAndRestore();
            });
            if (m_part && m_part->widget()) {
                QTimer::singleShot(300, m_part->widget(), [this, w = m_part->widget()]() {
                    QCoreApplication::postEvent(w, new QEvent(QEvent::WindowActivate));
                    QCoreApplication::postEvent(w, new QResizeEvent(w->size(), w->size()));
                    QCoreApplication::postEvent(w, new QEnterEvent(QPointF(0,0), QPointF(0,0), QPointF(0,0)));
                    QCoreApplication::postEvent(w, new QEvent(QEvent::Leave));
                    w->update();
                    
                    for (QWidget *child : w->findChildren<QWidget*>()) {
                        QCoreApplication::postEvent(child, new QEvent(QEvent::WindowActivate));
                        QCoreApplication::postEvent(child, new QResizeEvent(child->size(), child->size()));
                        child->update();
                    }
                    
                    if (m_selectedPart.pluginId() == QLatin1String("markdownpart")) {
                        QScrollBar *vbar = nullptr;
                        QAbstractScrollArea *scrollArea = w->findChild<QAbstractScrollArea*>();
                        if (scrollArea) {
                            vbar = scrollArea->verticalScrollBar();
                        }
                        if (!vbar) {
                            for (QScrollBar *bar : w->findChildren<QScrollBar*>()) {
                                if (bar->orientation() == Qt::Vertical && bar->maximum() > 0) {
                                    vbar = bar;
                                    break;
                                }
                            }
                        }
                        if (vbar) {
                            vbar->setValue(0);
                        }
                    } else if (m_selectedPart.pluginId() == QLatin1String("okularpart")) {
                        // Enable text selection tool by default in Okular so users can copy text.
                        QAction *textSelect = m_part->actionCollection() ? m_part->actionCollection()->action(QStringLiteral("mouse_textselect")) : nullptr;
                        if (!textSelect) {
                            textSelect = m_part->findChild<QAction*>(QStringLiteral("mouse_textselect"), Qt::FindChildrenRecursively);
                        }
                        if (textSelect) {
                            textSelect->trigger();
                        }
                    }
                });
            }
        });
        KParts::OpenUrlArguments args;
        args.setReload(true);
        m_part->setArguments(args);
        m_part->openUrl(m_pendingUrl);

        // Immediately restore focus after opening (catches synchronous focus steals)
        restoreFocusToDC();
    }
}



bool KPartWidget::scrollView(int key)
{
    if (!m_part || !m_part->widget()) return false;

    // Find the vertical scrollbar inside the KPart's widget tree.
    // Try QAbstractScrollArea first (Okular, Gwenview, etc.), then
    // fall back to any vertical QScrollBar (KTextEditor uses KateScrollBar).
    QScrollBar *vbar = nullptr;

    QAbstractScrollArea *scrollArea = m_part->widget()->findChild<QAbstractScrollArea*>();
    if (scrollArea) {
        vbar = scrollArea->verticalScrollBar();
    }

    if (!vbar || !vbar->maximum()) {
        for (QScrollBar *bar : m_part->widget()->findChildren<QScrollBar*>()) {
            if (bar->orientation() == Qt::Vertical && bar->maximum() > 0) {
                vbar = bar;
                break;
            }
        }
    }

    if (!vbar || vbar->maximum() <= 0) return false;

    switch (key) {
    case Qt::Key_Up:
        vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
        return true;
    case Qt::Key_Down:
        vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        return true;
    case Qt::Key_PageUp:
        vbar->triggerAction(QAbstractSlider::SliderPageStepSub);
        return true;
    case Qt::Key_PageDown:
        vbar->triggerAction(QAbstractSlider::SliderPageStepAdd);
        return true;
    case Qt::Key_Home:
        vbar->triggerAction(QAbstractSlider::SliderToMinimum);
        return true;
    case Qt::Key_End:
        vbar->triggerAction(QAbstractSlider::SliderToMaximum);
        return true;
    }
    return false;
}

void KPartWidget::restoreZoom()
{
    if (!m_part) return;
    
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("doublecmd"), QStringLiteral("kpartview"));
    
    static const QString zoomFit = QStringLiteral("view_zoom_to_fit");
    static const QString zoomActual = QStringLiteral("view_actual_size");
    
    QString activeZoom;
    if (settings.contains(zoomActual) && settings.value(zoomActual).toBool()) {
        activeZoom = zoomActual;
    } else if (settings.contains(zoomFit) && settings.value(zoomFit).toBool()) {
        activeZoom = zoomFit;
    } else {
        activeZoom = zoomFit; // Default fallback if both are false or unconfigured
    }
    
    QAction *zoomFitAct = nullptr, *zoomActualAct = nullptr;
    QList<QAction*> actions;
    if (m_part->actionCollection()) {
        actions = m_part->actionCollection()->actions();
    }
    const auto childActions = m_part->findChildren<QAction*>(Qt::FindChildrenRecursively);
    for (QAction *a : childActions) {
        if (!actions.contains(a)) actions.append(a);
    }
    for (QAction *act : actions) {
        if (act->objectName() == zoomFit) zoomFitAct = act;
        else if (act->objectName() == zoomActual) zoomActualAct = act;
    }
    
    if (!activeZoom.isEmpty() && (zoomFitAct || zoomActualAct)) {
        QAction *activeAct = (activeZoom == zoomFit) ? zoomFitAct : zoomActualAct;
        qDebug() << "[KPartWidget] restoreZoom: activeZoom=" << activeZoom
                 << "currently checked=" << (activeAct ? activeAct->isChecked() : false);
        if (activeAct) {
            if (activeAct->isChecked()) {
                activeAct->blockSignals(true);
                activeAct->setChecked(false);
                activeAct->blockSignals(false);
            }
            activeAct->trigger();
        }
    }
}
