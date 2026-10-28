#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QDateTime>

namespace MopMail {

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
    };

    struct Filter {
        qint64 accountId = 0; // 0 = alle Konten
        qint64 folderId = 0;  // > 0: genau dieser Ordner
        QString role;         // sonst: alle Ordner dieser Rolle
        QString search;       // FTS5-Suche, überschreibt Ordnerauswahl
    };

    explicit MessageListModel(QObject *parent = nullptr);

    void setFilter(const Filter &filter);
    Filter filter() const { return m_filter; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    // Aktualisiert Flags einer Zeile ohne Neuladen der ganzen Liste.
    void updateFlags(qint64 messageId, int flags);
    void removeMessage(qint64 messageId);

    Q_INVOKABLE int rowOf(qint64 messageId) const;

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
    };

    QList<Row> fetchPage(int offset, int limit) const;
    static QString formatDate(const QDateTime &date);
    static QString ftsQuery(const QString &text);

    Filter m_filter;
    QList<Row> m_rows;
    bool m_hasMore = false;
};

} // namespace MopMail
