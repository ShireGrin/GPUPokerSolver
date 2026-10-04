#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <vector>

class DBManager {
    public:
        static DBManager& instance();

        bool init();
        QSqlDatabase db();

        // Helper functions for mirroring data
        int getOrCreateSite(const QString& siteName);
        int getOrCreateTourney(int siteId, const QString& tourneyNo, double buyin, const QDateTime& datePlayed);
        int getOrCreatePlayer(int siteId, const QString& playerName);
        int getOrCreateHand(
            int tourneyId, const QString& handNo, const QDateTime& datePlayed,
            int cntPlayers, double bbSize, double ante, double pot,
            double effectiveStack, const QString& board, int ipPlayerId,
            int oopPlayerId, const QString& turnCard, const QString& riverCard
        );

        // Profile methods
        bool loadProfilesFromJson(const QString& jsonFilePath);
        QJsonObject getProfileForPlayer(int playerId, double stackBb, double blindLevel);
        QJsonObject getProfileForPlayerByName(const QString& playerName, int siteId, double stackBb, double blindLevel);
        QString getClusterForPlayerName(const QString& playerName);
        QString getDataDrivenRange(const QString& clusterName, const QString& scenarioName, const QString& positionName, double stackBb);

        // GTO Decisions & Solves
        bool logGtoDecision(
            int handId, int playerId, const QString& street,
            const QString& actionSequence, const QString& actionTaken,
            const QString& gtoAction, double evAction, double evGto, double evLoss
        );
        int saveSolve(
                const QVariant& handId, const QString& configText,
                double exploitability, const QByteArray& strategyBlob,
                bool is_node_locked = false, int parent_id_solve = -1
        );
        bool loadSolveById(int solveId, QString& configText, double& exploitability, QByteArray& strategyBlob);
        bool deleteSolve(int solveId);
        class QSqlQueryModel* getSolvesModel(QObject* parent = nullptr);

        // Neural Network Training Data
        bool updateTrainingSample(
            int row_id, const std::vector<float>& oop_range,
            const std::vector<float>& ip_range, const std::vector<float>& oop_evs
            const std::vector<float>& ip_evs, float exploitability,
            float solve_duration_sec
        );

        // Saved Parameters
        int saveParameter(const QString& name, const QString& text);
        bool loadParameter(int id, QString& text);
        bool deleteParameter(int id);
        class QSqlQueryModel* getParametersModel(QObject* parent = nullptr);

    private:
        DBManager();
        ~DBManager();
        DBManager(const DBManager&) = delete;
        DBManager& operator=(const DBManager&) = delete;

        QString m_host;
        int m_port;
        QString m_dbName;
        QString m_user;
        QString m_pass;
        bool m_initialized;
};

#endif // DBMANAGER_H
