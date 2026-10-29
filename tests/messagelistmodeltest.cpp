#include "core/accountmanager.h"
#include "models/messagelistmodel.h"
#include "storage/database.h"
#include "storage/demodata.h"
#include "storage/messageflags.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Fjordpost;

class MessageListModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));
        m_accounts.load();
        seedDemoData(m_accounts);
        QCOMPARE(m_accounts.accounts().size(), 3);
    }

    void unifiedInboxContainsAllAccounts()
    {
        MessageListModel model;
        model.setFilter({});
        QCOMPARE(model.rowCount(), 3 * 8);

        // Absteigend nach Datum sortiert
        for (int i = 1; i < model.rowCount(); ++i) {
            QVERIFY(model.index(i - 1).data(MessageListModel::DateRole).toDateTime()
                    >= model.index(i).data(MessageListModel::DateRole).toDateTime());
        }
    }

    void singleAccountFilter()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.accountId = m_accounts.accounts().first().id;
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 8);
    }

    void flaggedVirtualFolder()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.role = QString::fromLatin1(FolderRole::Flagged);
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 3);
        for (int i = 0; i < model.rowCount(); ++i)
            QVERIFY(model.index(i).data(MessageListModel::FlaggedRole).toBool());
    }

    void searchIgnoresSyntaxCharacters()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.search = QStringLiteral("Buchung\" *");
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 3);

        f.search = QStringLiteral("münch"); // Präfix + ohne Umlaut-Unterschied
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 3);
    }

private:
    QTemporaryDir m_dir;
    AccountManager m_accounts;
};

QTEST_GUILESS_MAIN(MessageListModelTest)
#include "messagelistmodeltest.moc"
