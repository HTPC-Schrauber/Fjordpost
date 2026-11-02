#pragma once

#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace Fjordpost {

class AccountManager;
class MailService;

namespace Mbox {
class Reader;
}

// Import und Export von .eml- und mbox-Dateien im Hintergrund: immer nur eine Nachricht
// je Durchlauf der Ereignisschleife, damit große Archive die Oberfläche nicht blockieren
// und nicht ganz im Speicher landen.
class MailTransfer : public QObject
{
    Q_OBJECT
public:
    MailTransfer(MailService *mail, AccountManager *accounts, QObject *parent = nullptr);
    ~MailTransfer() override;

    bool isBusy() const { return m_mode != Mode::Idle; }
    QString status() const { return m_status; }

    // Liefert eine Fehlermeldung oder einen leeren String, wenn der Vorgang läuft.
    QString startImport(qint64 folderId, const QStringList &paths);
    // eml: genau eine Nachricht unverändert schreiben, sonst mbox.
    QString startExport(const QList<qint64> &messageIds, const QString &path, bool eml);
    void cancel();

Q_SIGNALS:
    void busyChanged();
    void statusChanged();
    void finished(const QString &summary, qint64 folderId);

private:
    enum class Mode { Idle, Import, Export };

    void setStatus(const QString &status);
    void scheduleNext();
    void importNext();
    QByteArray nextRawMessage();
    void afterImported(bool ok);
    void exportNext();
    void writeExport(qint64 messageId);
    void finish(const QString &summary);

    MailService *m_mail;
    AccountManager *m_accounts;
    Mode m_mode = Mode::Idle;
    QString m_status;
    QTimer m_watchdog;
    int m_done = 0;
    int m_skipped = 0;

    // Import
    qint64 m_folderId = 0;
    qint64 m_accountId = 0;
    QStringList m_paths;
    QString m_failedFile;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<Mbox::Reader> m_reader;

    // Export
    QList<qint64> m_ids;
    int m_total = 0;
    bool m_eml = false;
    qint64 m_waitingFor = 0;
    std::unique_ptr<QSaveFile> m_output;
};

} // namespace Fjordpost
