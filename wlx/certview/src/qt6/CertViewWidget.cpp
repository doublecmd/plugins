#include "CertViewWidget.h"
#include <QLineEdit>
#include <QFileInfo>
#include <QDateTime>

namespace certview {

CertViewWidget::CertViewWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void CertViewWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // Banner Widget (Status, Selector, Actions)
    m_bannerWidget = new QWidget(this);
    auto *bannerLayout = new QHBoxLayout(m_bannerWidget);
    bannerLayout->setContentsMargins(4, 4, 4, 4);
    bannerLayout->setSpacing(8);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 12px; padding: 3px 6px; border-radius: 4px;");

    m_fileInfoLabel = new QLabel(this);
    m_fileInfoLabel->setStyleSheet("color: #888; font-size: 11px;");

    m_certSelector = new QComboBox(this);
    m_certSelector->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_certSelector->setStyleSheet("font-weight: bold; padding: 2px 6px;");
    connect(m_certSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CertViewWidget::onCertSelected);

    m_copySummaryBtn = new QPushButton(tr("Copy Summary"), this);
    m_copyPemBtn = new QPushButton(tr("Copy Raw / Key"), this);

    connect(m_copySummaryBtn, &QPushButton::clicked, this, &CertViewWidget::copySummaryToClipboard);
    connect(m_copyPemBtn, &QPushButton::clicked, this, &CertViewWidget::copyPemToClipboard);

    bannerLayout->addWidget(m_statusLabel);
    bannerLayout->addWidget(m_fileInfoLabel);
    bannerLayout->addWidget(m_certSelector);
    bannerLayout->addStretch();
    bannerLayout->addWidget(m_copySummaryBtn);
    bannerLayout->addWidget(m_copyPemBtn);

    mainLayout->addWidget(m_bannerWidget);

    // Unlock panel for PKCS#12
    m_unlockPanel = new QWidget(this);
    auto *unlockLayout = new QHBoxLayout(m_unlockPanel);
    unlockLayout->setContentsMargins(4, 2, 4, 2);
    auto *pwdPrompt = new QLabel(tr("Password:"), this);
    m_passwordInput = new QLineEdit(this);
    m_passwordInput->setEchoMode(QLineEdit::Password);
    m_unlockButton = new QPushButton(tr("Decrypt"), this);
    connect(m_unlockButton, &QPushButton::clicked, this, &CertViewWidget::onPasswordSubmitted);
    connect(m_passwordInput, &QLineEdit::returnPressed, this, &CertViewWidget::onPasswordSubmitted);

    unlockLayout->addWidget(pwdPrompt);
    unlockLayout->addWidget(m_passwordInput);
    unlockLayout->addWidget(m_unlockButton);
    unlockLayout->addStretch();
    m_unlockPanel->setVisible(false);
    mainLayout->addWidget(m_unlockPanel);

    // Tab Widget
    m_tabWidget = new QTabWidget(this);

    // 1. Summary Table
    m_summaryTable = new QTableWidget(this);
    m_summaryTable->setColumnCount(2);
    m_summaryTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    m_summaryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_summaryTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_summaryTable->verticalHeader()->setVisible(false);
    m_summaryTable->setAlternatingRowColors(true);
    m_summaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    // 2. Extensions Table
    m_extensionsTable = new QTableWidget(this);
    m_extensionsTable->setColumnCount(2);
    m_extensionsTable->setHorizontalHeaderLabels({tr("Attribute / Extension"), tr("Value")});
    m_extensionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_extensionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_extensionsTable->verticalHeader()->setVisible(false);
    m_extensionsTable->setAlternatingRowColors(true);
    m_extensionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_extensionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    // 3. Raw / Text View
    m_rawTextEdit = new QTextEdit(this);
    m_rawTextEdit->setReadOnly(true);
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    m_rawTextEdit->setFont(font);

    m_tabWidget->addTab(m_summaryTable, tr("General"));
    m_tabWidget->addTab(m_extensionsTable, tr("Extensions & SANs"));
    m_tabWidget->addTab(m_rawTextEdit, tr("Details / Raw"));

    mainLayout->addWidget(m_tabWidget);
}

void CertViewWidget::addRow(QTableWidget *table, const QString &field, const QString &value)
{
    int row = table->rowCount();
    table->insertRow(row);
    auto *itemField = new QTableWidgetItem(field);
    QFont boldFont = itemField->font();
    boldFont.setBold(true);
    itemField->setFont(boldFont);

    auto *itemVal = new QTableWidgetItem(value);
    table->setItem(row, 0, itemField);
    table->setItem(row, 1, itemVal);
}

bool CertViewWidget::loadFile(const QString &filePath, const QString &password)
{
    m_filePath = filePath;
    m_doc = CertParser::parseFile(filePath.toStdString(), password.toStdString());

    if (m_doc.overallStatus == CertStatus::Corrupted && m_doc.fileType == FileType::Unknown) {
        return false;
    }

    populateDocument(m_doc);
    return true;
}

void CertViewWidget::populateDocument(const CertDocument &doc)
{
    QFileInfo fi(m_filePath);
    m_fileInfoLabel->setText(QString("%1 (%2 bytes)").arg(fi.fileName()).arg(doc.fileSize));

    // Status label styling
    QString stText = QString::fromStdString(doc.statusText);
    if (stText.isEmpty()) stText = tr("✓ VALID");
    m_statusLabel->setText(stText);

    switch (doc.overallStatus) {
        case CertStatus::Valid:
            m_statusLabel->setStyleSheet("background-color: #2e7d32; color: white; font-weight: bold; border-radius: 4px; padding: 3px 6px;");
            m_unlockPanel->setVisible(false);
            break;
        case CertStatus::Expired:
            m_statusLabel->setStyleSheet("background-color: #c62828; color: white; font-weight: bold; border-radius: 4px; padding: 3px 6px;");
            m_unlockPanel->setVisible(false);
            break;
        case CertStatus::NotYetValid:
            m_statusLabel->setStyleSheet("background-color: #ef6c00; color: white; font-weight: bold; border-radius: 4px; padding: 3px 6px;");
            m_unlockPanel->setVisible(false);
            break;
        case CertStatus::EncryptedContainer:
            m_statusLabel->setStyleSheet("background-color: #1565c0; color: white; font-weight: bold; border-radius: 4px; padding: 3px 6px;");
            m_unlockPanel->setVisible(true);
            break;
        case CertStatus::InfoOnly:
            m_statusLabel->setStyleSheet("background-color: #00838f; color: white; font-weight: bold; border-radius: 4px; padding: 3px 6px;");
            m_unlockPanel->setVisible(false);
            break;
        case CertStatus::Corrupted:
        default:
            m_statusLabel->setStyleSheet("background-color: #424242; color: white; font-weight: bold; border-radius: 4px; padding: 3px 6px;");
            m_unlockPanel->setVisible(false);
            break;
    }

    // Populate Selector ComboBox
    m_certSelector->blockSignals(true);
    m_certSelector->clear();

    if (doc.certificates.size() > 1) {
        for (size_t i = 0; i < doc.certificates.size(); ++i) {
            QString name = QString::fromStdString(doc.certificates[i].displayName);
            if (name.isEmpty()) name = QString("Entry #%1").arg(i + 1);
            m_certSelector->addItem(QString("[%1/%2] %3").arg(i + 1).arg(doc.certificates.size()).arg(name));
        }
        m_certSelector->setVisible(true);
    } else {
        m_certSelector->setVisible(false);
    }
    m_certSelector->blockSignals(false);

    if (doc.fileType == FileType::PKCS12Container) {
        m_summaryTable->setRowCount(0);
        m_extensionsTable->setRowCount(0);
        addRow(m_summaryTable, tr("Container Type"), "PKCS#12 v3 (.pfx / .p12)");
        addRow(m_summaryTable, tr("Protection"), tr("Password Protected / Encrypted Safe Bags"));
        addRow(m_summaryTable, tr("Action Required"), tr("Enter password above or extract using OpenSSL CLI"));
        m_rawTextEdit->setText(QString::fromStdString(doc.fullTextSummary));
        return;
    }

    if (!doc.certificates.empty()) {
        populateSingleCert(doc.certificates[0]);
    }
}

void CertViewWidget::onCertSelected(int index)
{
    if (index >= 0 && index < (int)m_doc.certificates.size()) {
        populateSingleCert(m_doc.certificates[index]);
    }
}

void CertViewWidget::populateSingleCert(const CertEntry &c)
{
    m_summaryTable->setRowCount(0);
    m_extensionsTable->setRowCount(0);

    // General Summary
    if (!c.displayName.empty()) addRow(m_summaryTable, tr("Name / ID"), QString::fromStdString(c.displayName));
    if (!c.subject.empty()) addRow(m_summaryTable, tr("Subject"), QString::fromStdString(c.subject));
    if (!c.issuer.empty()) addRow(m_summaryTable, tr("Issuer / Type"), QString::fromStdString(c.issuer));
    if (!c.validFrom.empty()) addRow(m_summaryTable, tr("Valid From"), QString::fromStdString(c.validFrom));
    if (!c.validTo.empty()) addRow(m_summaryTable, tr("Valid Until"), QString::fromStdString(c.validTo));
    
    if (c.daysRemaining > 0) {
        addRow(m_summaryTable, tr("Validity"), tr("Valid (%1 days remaining)").arg(c.daysRemaining));
    } else if (c.isExpired) {
        addRow(m_summaryTable, tr("Validity"), tr("Expired"));
    } else if (c.isNotYetValid) {
        addRow(m_summaryTable, tr("Validity"), tr("Not Yet Valid"));
    }

    if (!c.serialNumber.empty()) addRow(m_summaryTable, tr("Serial / Key ID"), QString::fromStdString(c.serialNumber));
    if (!c.publicKeyAlgorithm.empty()) {
        QString pkStr = QString::fromStdString(c.publicKeyAlgorithm);
        if (c.publicKeyBits > 0) pkStr += QString(" (%1 bits)").arg(c.publicKeyBits);
        addRow(m_summaryTable, tr("Public Key"), pkStr);
    }
    if (!c.signatureAlgorithm.empty()) addRow(m_summaryTable, tr("Signature Algorithm"), QString::fromStdString(c.signatureAlgorithm));
    if (!c.sha256Fingerprint.empty()) addRow(m_summaryTable, tr("SHA-256 Fingerprint"), QString::fromStdString(c.sha256Fingerprint));
    if (!c.sha1Fingerprint.empty()) addRow(m_summaryTable, tr("Secondary Fingerprint"), QString::fromStdString(c.sha1Fingerprint));
    if (!c.version.empty()) addRow(m_summaryTable, tr("Version"), QString::fromStdString(c.version));

    // Extensions & SANs
    for (const auto &san : c.subjectAltNames) {
        addRow(m_extensionsTable, tr("Identity / Alt Name"), QString::fromStdString(san));
    }
    for (const auto &ku : c.keyUsage) {
        addRow(m_extensionsTable, tr("Key Usage"), QString::fromStdString(ku));
    }
    for (const auto &eku : c.extKeyUsage) {
        addRow(m_extensionsTable, tr("Extended Key Usage"), QString::fromStdString(eku));
    }

    // Raw Details Tab
    QString detailsText = QString::fromStdString(c.humanReadableText);
    if (!c.rawPem.empty()) {
        detailsText += "\n\n----- Raw Data / PEM -----\n" + QString::fromStdString(c.rawPem);
    }
    m_rawTextEdit->setText(detailsText);
}

void CertViewWidget::copySummaryToClipboard()
{
    QApplication::clipboard()->setText(QString::fromStdString(m_doc.fullTextSummary));
}

void CertViewWidget::copyPemToClipboard()
{
    int idx = m_certSelector->currentIndex();
    if (idx < 0) idx = 0;
    if (idx < (int)m_doc.certificates.size() && !m_doc.certificates[idx].rawPem.empty()) {
        QApplication::clipboard()->setText(QString::fromStdString(m_doc.certificates[idx].rawPem));
    } else {
        QApplication::clipboard()->setText(QString::fromStdString(m_doc.fullTextSummary));
    }
}

void CertViewWidget::onPasswordSubmitted()
{
    QString pwd = m_passwordInput->text();
    loadFile(m_filePath, pwd);
}

} // namespace certview
