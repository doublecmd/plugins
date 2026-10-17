#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>

#include "core/CertModel.h"
#include "core/CertParser.h"

namespace certview {

class CertViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit CertViewWidget(QWidget *parent = nullptr);
    bool loadFile(const QString &filePath, const QString &password = "");

public slots:
    void copySummaryToClipboard();
    void copyPemToClipboard();
    void onPasswordSubmitted();
    void onCertSelected(int index);

private:
    void setupUi();
    void populateDocument(const CertDocument &doc);
    void populateSingleCert(const CertEntry &cert);
    void addRow(QTableWidget *table, const QString &field, const QString &value);

    CertDocument m_doc;
    QString m_filePath;

    QTabWidget *m_tabWidget = nullptr;
    
    // Status banner
    QWidget *m_bannerWidget = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_fileInfoLabel = nullptr;
    QComboBox *m_certSelector = nullptr;

    // Tabs
    QTableWidget *m_summaryTable = nullptr;
    QTableWidget *m_extensionsTable = nullptr;
    QTextEdit *m_rawTextEdit = nullptr;
    
    // PKCS#12 unlock widgets
    QWidget *m_unlockPanel = nullptr;
    class QLineEdit *m_passwordInput = nullptr;
    QPushButton *m_unlockButton = nullptr;

    QPushButton *m_copySummaryBtn = nullptr;
    QPushButton *m_copyPemBtn = nullptr;
};

} // namespace certview
