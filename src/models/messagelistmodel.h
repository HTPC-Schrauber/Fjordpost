#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QDateTime>
#include <QSet>

class QSqlQuery;

namespace Fjordpost {

// Nachrichtenliste für einen Ordner, einen Sammelordner (Rolle über alle
// bzw. ein Konto) oder eine Volltextsuche. Lädt seitenweise nach (fetchMore).
class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        MessageIdRole = Qt::UserRole + 1,
        SubjectRole,
        FromNameRole,
        FromEmailRole,
        DateRole,
        DateTextRole,
        SnippetRole,
        UnreadRole,
        FlaggedRole,
        AnsweredRole,
        HasAttachmentsRole,
        AccountColorRole,
        DateGroupRole,
        InitialsRole,
        FolderNameRole,
        ThreadCountRole,
        ExpandedRole,  // Konversation aufgeklappt
        IsChildRole,   // ältere Nachricht unter einer aufgeklappten Konversation
        SentRole,      // liegt in "Gesendet"
    };

    struct Filter {
        qint64 accountId = 0; // 0 = alle Konten
        qint64 folderId = 0;  // > 0: genau dieser Ordner
        QString role;         // sonst: alle Ordner dieser Rolle
        QString search;       // FTS5-Suche, überschreibt Ordnerauswahl
        QString quick;        // Schnellfilter: "", "unread", "flagged", "attachments"
        bool threaded = false; // eine Zeile pro Konversation (neueste passende Nachricht)
    };

    explicit MessageListModel(QObject *parent = nullptr);

    void setFilter(const Filter &filter);
    Filter filter() const { return m_filter; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    // Aktualisiert Flags einer Zeile ohne Neuladen der ganzen Liste
    // (in der Konversationsansicht wird neu abgefragt, da andere Nachrichten mitzählen).
    void updateFlags(qint64 messageId, int flags);
    void removeMessage(qint64 messageId);

    // Klappt die Konversation der Zeile auf bzw. zu.
    Q_INVOKABLE void toggleThread(qint64 messageId);
    Q_INVOKABLE int rowOf(qint64 messageId) const;
    Q_INVOKABLE qint64 idAt(int row) const;

public Q_SLOTS:
    void reload();
    // Nach Hintergrund-Änderungen: aktualisiert vorhandene Zeilen und fügt
    // neue ein bzw. entfernt verschwundene, ohne die Liste neu aufzubauen.
    void refresh();

Q_SIGNALS:
    void countChanged();

private:
    struct Row {
        qint64 id = 0;
        QString subject;
        QString fromName;
        QString fromEmail;
        QDateTime date;
        QString snippet;
        int flags = 0;
        bool hasAttachments = false;
        QColor accountColor;
        QString folderName;
        int threadCount = 1;
        int threadUnread = 0;
        qint64 threadId = 0;
        bool child = false;
        bool sent = false;
        QDateTime groupDate; // Datumsgruppe; bei Unterzeilen die der Konversation

        bool operator==(const Row &) const = default;
    };

    QList<Row> fetchPage(int offset, int limit) const;
    // Fügt unter aufgeklappten Konversationen deren übrige Nachrichten ein.
    QList<Row> withChildren(const QList<Row> &topLevel) const;
    static Row rowFromQuery(const QSqlQuery &q);
    static QString formatDate(const QDateTime &date);
    static QString dateGroup(const QDateTime &date);
    static QString ftsQuery(const QString &text);

    Filter m_filter;
    QList<Row> m_rows;
    int m_topLevelCount = 0; // Zeilen ohne Unterzeilen (für das seitenweise Nachladen)
    QSet<qint64> m_expandedThreads;
    bool m_hasMore = false;
};

} // namespace Fjordpost
