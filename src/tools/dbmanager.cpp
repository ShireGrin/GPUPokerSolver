#include <algorithm>
#include "include/tools/dbmanager.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVariant>
#include <iostream>
#include <QSqlQueryModel>

DBManager::DBManager() :
    m_host(qEnvironmentVariable("DB_SOLVER_HOST", "localhost")),
    m_port(qEnvironmentVariable("DB_SOLVER_PORT", "5432").toInt()),
    m_dbName(qEnvironmentVariable("DB_SOLVER", "db_name")),
    m_user(qEnvironmentVariable("DB_SOLVER_USER", "postgres")),
    m_pass(qEnvironmentVariable("DB_SOLVER_PASSWORD", "dbpass")),
    m_initialized(false)
{
}

DBManager::~DBManager() {
    // Intentionally empty. Do not touch QSqlDatabase here because this runs 
    // during static teardown after QCoreApplication might be partially destroyed.
}

DBManager& DBManager::instance() {
    static DBManager inst;
    return inst;
}

bool DBManager::init() {
    if (m_initialized) return true;

    QSqlDatabase db;
    if (QSqlDatabase::contains("texassolver_conn")) {
        db = QSqlDatabase::database("texassolver_conn");
    } else {
        db = QSqlDatabase::addDatabase("QPSQL", "texassolver_conn");
    }

    db.setHostName(m_host);
    db.setPort(m_port);
    db.setDatabaseName(m_dbName);
    db.setUserName(m_user);
    db.setPassword(m_pass);

    if (!db.open()) {
        qWarning() << "Failed to open standalone database:" << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    QStringList tables = {
        "CREATE TABLE IF NOT EXISTS sites (id_site SERIAL PRIMARY KEY, site_name VARCHAR(255) UNIQUE)",
        "CREATE TABLE IF NOT EXISTS tourneys (id_tourney SERIAL PRIMARY KEY, id_site INT, tourney_no VARCHAR(255), buyin FLOAT, date_played TIMESTAMP, UNIQUE(id_site, tourney_no))",
        "CREATE TABLE IF NOT EXISTS players (id_player SERIAL PRIMARY KEY, id_site INT, player_name VARCHAR(255), UNIQUE(id_site, player_name))",
        "CREATE TABLE IF NOT EXISTS profile_clusters (id_cluster SERIAL PRIMARY KEY, cluster_name VARCHAR(255) UNIQUE, vpip_mean FLOAT, pfr_mean FLOAT, threebet_mean FLOAT, af_mean FLOAT, wtsd_mean FLOAT, ranges_json JSONB, betting_profiles_json JSONB)",
        "CREATE TABLE IF NOT EXISTS player_profiles (id_player INT, id_cluster INT, vpip FLOAT, pfr FLOAT, threebet FLOAT, af FLOAT, wtsd FLOAT, cnt_hands INT, stack_depth_bb_min FLOAT, stack_depth_bb_max FLOAT, blind_level_min FLOAT, blind_level_max FLOAT, PRIMARY KEY(id_player, stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max))",
        "CREATE TABLE IF NOT EXISTS hands (id_hand SERIAL PRIMARY KEY, id_tourney INT, hand_no VARCHAR(255), date_played TIMESTAMP, cnt_players INT, bb_size FLOAT, ante FLOAT, pot FLOAT, effective_stack FLOAT, board VARCHAR(255), ip_player_id INT, oop_player_id INT, turn_card VARCHAR(10), river_card VARCHAR(10), UNIQUE(id_tourney, hand_no))",
        "CREATE TABLE IF NOT EXISTS player_gto_decisions (id_decision SERIAL PRIMARY KEY, id_hand INT, id_player INT, street VARCHAR(20), action_sequence TEXT, action_taken VARCHAR(50), gto_action VARCHAR(50), ev_action FLOAT, ev_gto FLOAT, ev_loss FLOAT)",
        "CREATE TABLE IF NOT EXISTS solves (id_solve SERIAL PRIMARY KEY, id_hand INT, solve_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP, config_text TEXT, exploitability FLOAT, strategy_blob BYTEA)",
        "CREATE TABLE IF NOT EXISTS saved_parameters (id_param SERIAL PRIMARY KEY, param_name VARCHAR(255) UNIQUE, param_text TEXT, created_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS positions (id_position SERIAL PRIMARY KEY, name VARCHAR(10) UNIQUE NOT NULL, seat_order INT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS action_scenarios (id_scenario SERIAL PRIMARY KEY, name VARCHAR(40) UNIQUE NOT NULL, description TEXT, frequency SMALLINT NOT NULL DEFAULT 128, typical_pot_bb REAL NOT NULL DEFAULT 6.5, parent_scenario_id INT REFERENCES action_scenarios(id_scenario))",
        "CREATE TABLE IF NOT EXISTS stack_clusters (id_stack_cluster SERIAL PRIMARY KEY, cluster_name VARCHAR(50) UNIQUE NOT NULL, bb_min REAL NOT NULL, bb_max REAL NOT NULL)",
        "CREATE TABLE IF NOT EXISTS cluster_ranges (id SERIAL PRIMARY KEY, id_cluster INT NOT NULL REFERENCES profile_clusters(id_cluster) ON DELETE CASCADE, id_position INT NOT NULL REFERENCES positions(id_position), id_scenario INT NOT NULL REFERENCES action_scenarios(id_scenario), id_stack_cluster INT NOT NULL REFERENCES stack_clusters(id_stack_cluster), range_text TEXT NOT NULL, UNIQUE(id_cluster, id_position, id_scenario, id_stack_cluster))",
        "CREATE TABLE IF NOT EXISTS training_samples (id SERIAL PRIMARY KEY, board_text VARCHAR(15), oop_range_text TEXT, ip_range_text TEXT, board_card0 SMALLINT, board_card1 SMALLINT, board_card2 SMALLINT, pot REAL NOT NULL, effective_stack REAL NOT NULL, spr REAL NOT NULL, oop_range REAL[], ip_range REAL[], oop_evs REAL[], ip_evs REAL[], exploitability REAL, status VARCHAR(20) DEFAULT 'pending', solve_duration_sec REAL, id_cluster_oop INT REFERENCES profile_clusters(id_cluster), id_cluster_ip INT REFERENCES profile_clusters(id_cluster), id_position_oop INT REFERENCES positions(id_position), id_position_ip INT REFERENCES positions(id_position), id_scenario INT REFERENCES action_scenarios(id_scenario), id_stack_cluster INT REFERENCES stack_clusters(id_stack_cluster), created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)"
    };

    for (const QString& queryStr : tables) {
        if (!q.exec(queryStr)) {
            qWarning() << "Failed to create table:" << q.lastError().text() << "Query:" << queryStr;
        }
    }

    // Non-destructive additions for node locking and lineage
    q.exec("ALTER TABLE solves ADD COLUMN IF NOT EXISTS is_node_locked BOOLEAN DEFAULT FALSE");
    q.exec("ALTER TABLE solves ADD COLUMN IF NOT EXISTS parent_id_solve INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE solves ADD COLUMN IF NOT EXISTS size_bytes BIGINT DEFAULT 0");

    // Chunking to avoid 1GB bytea output limit in Postgres
    q.exec("ALTER TABLE solves DROP COLUMN IF EXISTS strategy_blob");
    q.exec("CREATE TABLE IF NOT EXISTS solve_chunks (id_solve INT REFERENCES solves(id_solve) ON DELETE CASCADE, chunk_idx INT, chunk_data BYTEA, PRIMARY KEY(id_solve, chunk_idx))");

    m_initialized = true;
    qDebug() << "Successfully connected to standalone PostgreSQL database";
    return true;
}

QSqlDatabase DBManager::db() {
    init();
    return QSqlDatabase::database("texassolver_conn");
}

int DBManager::getOrCreateSite(const QString& siteName) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return -1;

    QSqlQuery q(d);
    q.prepare("SELECT id_site FROM sites WHERE site_name = :name");
    q.bindValue(":name", siteName);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    q.prepare("INSERT INTO sites (site_name) VALUES (:name) RETURNING id_site");
    q.bindValue(":name", siteName);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    qWarning() << "getOrCreateSite failed:" << q.lastError().text();
    return -1;
}

int DBManager::getOrCreateTourney(int siteId, const QString& tourneyNo, double buyin, const QDateTime& datePlayed) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return -1;

    QSqlQuery q(d);
    q.prepare("SELECT id_tourney FROM tourneys WHERE id_site = :sid AND tourney_no = :no");
    q.bindValue(":sid", siteId);
    q.bindValue(":no", tourneyNo);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    q.prepare("INSERT INTO tourneys (id_site, tourney_no, buyin, date_played) "
              "VALUES (:sid, :no, :buyin, :date) RETURNING id_tourney");
    q.bindValue(":sid", siteId);
    q.bindValue(":no", tourneyNo);
    q.bindValue(":buyin", buyin);
    q.bindValue(":date", datePlayed);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    qWarning() << "getOrCreateTourney failed:" << q.lastError().text();
    return -1;
}

int DBManager::getOrCreatePlayer(int siteId, const QString& playerName) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return -1;

    QSqlQuery q(d);
    q.prepare("SELECT id_player FROM players WHERE id_site = :sid AND player_name = :name");
    q.bindValue(":sid", siteId);
    q.bindValue(":name", playerName);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    q.prepare("INSERT INTO players (id_site, player_name) VALUES (:sid, :name) RETURNING id_player");
    q.bindValue(":sid", siteId);
    q.bindValue(":name", playerName);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    qWarning() << "getOrCreatePlayer failed:" << q.lastError().text();
    return -1;
}

int DBManager::getOrCreateHand(int tourneyId, const QString& handNo, const QDateTime& datePlayed, 
                              int cntPlayers, double bbSize, double ante, double pot, 
                              double effectiveStack, const QString& board, int ipPlayerId, 
                              int oopPlayerId, const QString& turnCard, const QString& riverCard) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return -1;

    QSqlQuery q(d);
    q.prepare("SELECT id_hand FROM hands WHERE id_tourney = :tid AND hand_no = :no");
    q.bindValue(":tid", tourneyId);
    q.bindValue(":no", handNo);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    q.prepare("INSERT INTO hands (id_tourney, hand_no, date_played, cnt_players, bb_size, ante, pot, "
              "effective_stack, board, ip_player_id, oop_player_id, turn_card, river_card) "
              "VALUES (:tid, :no, :date, :cnt, :bb, :ante, :pot, :eff, :board, :ip, :oop, :turn, :river) "
              "RETURNING id_hand");
    q.bindValue(":tid", tourneyId);
    q.bindValue(":no", handNo);
    q.bindValue(":date", datePlayed);
    q.bindValue(":cnt", cntPlayers);
    q.bindValue(":bb", bbSize);
    q.bindValue(":ante", ante);
    q.bindValue(":pot", pot);
    q.bindValue(":eff", effectiveStack);
    q.bindValue(":board", board);
    q.bindValue(":ip", ipPlayerId > 0 ? QVariant(ipPlayerId) : QVariant(QVariant::Int));
    q.bindValue(":oop", oopPlayerId > 0 ? QVariant(oopPlayerId) : QVariant(QVariant::Int));
    q.bindValue(":turn", turnCard.isEmpty() ? QVariant(QVariant::String) : QVariant(turnCard));
    q.bindValue(":river", riverCard.isEmpty() ? QVariant(QVariant::String) : QVariant(riverCard));
    
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    qWarning() << "getOrCreateHand failed:" << q.lastError().text();
    return -1;
}

bool DBManager::loadProfilesFromJson(const QString& jsonFilePath) {
    QFile file(jsonFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open profiles JSON for loading:" << jsonFilePath;
        return false;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "Invalid JSON document in profiles file";
        return false;
    }

    QJsonObject root = doc.object();
    QJsonObject profiles = root["profiles"].toObject();

    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    // Start transaction
    d.transaction();

    QSqlQuery q(d);
    // Insert clusters
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        QString clusterKey = it.key();
        QJsonObject clusterData = it.value().toObject();
        QString profileName = clusterData["profile_name"].toString();

        QJsonObject stats = clusterData["stats"].toObject();
        double vpip = stats["VPIP"].toDouble();
        double pfr = stats["PFR"].toDouble();
        double threebet = stats["3Bet"].toDouble();
        double af = stats["AF"].toDouble();
        double wtsd = stats["WTSD"].toDouble();

        QJsonObject ranges = clusterData["ranges"].toObject();
        QJsonDocument rangesDoc(ranges);
        QString rangesJson = QString::fromUtf8(rangesDoc.toJson(QJsonDocument::Compact));

        // Remove betting keys to store separately in betting_profiles_json
        QJsonObject betting;
        for (const QString& k : clusterData.keys()) {
            if (k != "stats" && k != "profile_name" && k != "ranges") {
                betting.insert(k, clusterData[k]);
            }
        }
        QJsonDocument bettingDoc(betting);
        QString bettingJson = QString::fromUtf8(bettingDoc.toJson(QJsonDocument::Compact));

        q.prepare("INSERT INTO profile_clusters (cluster_name, vpip_mean, pfr_mean, threebet_mean, af_mean, wtsd_mean, ranges_json, betting_profiles_json) "
                  "VALUES (:name, :vpip, :pfr, :threebet, :af, :wtsd, CAST(:ranges AS jsonb), CAST(:betting AS jsonb)) "
                  "ON CONFLICT (cluster_name) DO UPDATE SET "
                  "vpip_mean = EXCLUDED.vpip_mean, pfr_mean = EXCLUDED.pfr_mean, threebet_mean = EXCLUDED.threebet_mean, "
                  "af_mean = EXCLUDED.af_mean, wtsd_mean = EXCLUDED.wtsd_mean, ranges_json = EXCLUDED.ranges_json, betting_profiles_json = EXCLUDED.betting_profiles_json");

        q.bindValue(":name", profileName);
        q.bindValue(":vpip", vpip);
        q.bindValue(":pfr", pfr);
        q.bindValue(":threebet", threebet);
        q.bindValue(":af", af);
        q.bindValue(":wtsd", wtsd);
        q.bindValue(":ranges", rangesJson);
        q.bindValue(":betting", bettingJson);

        if (!q.exec()) {
            qWarning() << "Failed to insert cluster:" << q.lastError().text();
            d.rollback();
            return false;
        }
    }

    // Insert player profiles mapped from json
    QJsonObject mappings = root["player_mapping"].toObject();
    for (auto it = mappings.begin(); it != mappings.end(); ++it) {
        QString playerName = it.key();
        QString profileName = it.value().toString();

        // First get or create player (we'll assume default site_id = 1 for now)
        int siteId = getOrCreateSite("PokerStars"); // default fallback site
        int playerId = getOrCreatePlayer(siteId, playerName);

        if (playerId < 0) continue;

        // Find cluster ID
        int clusterId = -1;
        q.prepare("SELECT id_cluster FROM profile_clusters WHERE cluster_name = :name");
        q.bindValue(":name", profileName);
        if (q.exec() && q.next()) {
            clusterId = q.value(0).toInt();
        }

        if (clusterId < 0) continue;

        q.prepare("INSERT INTO player_profiles (id_player, id_cluster, cnt_hands, stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max) "
                  "VALUES (:pid, :cid, 100, 0.0, 999.9, 0.0, 999999.9) "
                  "ON CONFLICT (id_player, stack_depth_bb_min, stack_depth_bb_max, blind_level_min, blind_level_max) DO UPDATE SET "
                  "id_cluster = EXCLUDED.id_cluster");
        q.bindValue(":pid", playerId);
        q.bindValue(":cid", clusterId);

        if (!q.exec()) {
            qWarning() << "Failed to insert player profile mapping:" << q.lastError().text();
            d.rollback();
            return false;
        }
    }

    d.commit();
    return true;
}

QJsonObject DBManager::getProfileForPlayer(int playerId, double stackBb, double blindLevel) {
    QJsonObject result;
    QSqlDatabase d = db();
    if (!d.isOpen()) return result;

    QSqlQuery q(d);
    q.prepare("SELECT c.cluster_name, c.ranges_json, c.betting_profiles_json "
              "FROM player_profiles p "
              "JOIN profile_clusters c ON p.id_cluster = c.id_cluster "
              "WHERE p.id_player = :pid "
              "AND :stack >= p.stack_depth_bb_min AND :stack <= p.stack_depth_bb_max "
              "AND :blind >= p.blind_level_min AND :blind <= p.blind_level_max");
    q.bindValue(":pid", playerId);
    q.bindValue(":stack", stackBb);
    q.bindValue(":blind", blindLevel);

    if (q.exec() && q.next()) {
        result.insert("profile_name", q.value(0).toString());

        QJsonDocument rangesDoc = QJsonDocument::fromJson(q.value(1).toString().toUtf8());
        result.insert("ranges", rangesDoc.object());

        QJsonDocument bettingDoc = QJsonDocument::fromJson(q.value(2).toString().toUtf8());
        QJsonObject bettingObj = bettingDoc.object();
        for (const QString& key : bettingObj.keys()) {
            result.insert(key, bettingObj[key]);
        }
    } else {
        // Fetch default Tight-Aggressive profile if not found
        q.prepare("SELECT cluster_name, ranges_json, betting_profiles_json FROM profile_clusters WHERE cluster_name = 'Tight-Aggressive (TAG)'");
        if (q.exec() && q.next()) {
            result.insert("profile_name", q.value(0).toString());
            QJsonDocument rangesDoc = QJsonDocument::fromJson(q.value(1).toString().toUtf8());
            result.insert("ranges", rangesDoc.object());
            QJsonDocument bettingDoc = QJsonDocument::fromJson(q.value(2).toString().toUtf8());
            QJsonObject bettingObj = bettingDoc.object();
            for (const QString& key : bettingObj.keys()) {
                result.insert(key, bettingObj[key]);
            }
        }
    }
    return result;
}

QJsonObject DBManager::getProfileForPlayerByName(const QString& playerName, int siteId, double stackBb, double blindLevel) {
    int pid = getOrCreatePlayer(siteId, playerName);
    if (pid < 0) return QJsonObject();
    return getProfileForPlayer(pid, stackBb, blindLevel);
}

QString DBManager::getClusterForPlayerName(const QString& playerName) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return "Unknown";

    QSqlQuery q(d);
    q.prepare("SELECT c.cluster_name "
              "FROM player_profiles pp "
              "JOIN players p ON pp.id_player = p.id_player "
              "JOIN profile_clusters c ON pp.id_cluster = c.id_cluster "
              "WHERE p.player_name = :pname "
              "ORDER BY pp.stack_depth_bb_max DESC "
              "LIMIT 1");
    q.bindValue(":pname", playerName);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return "Unknown";
}

QString DBManager::getDataDrivenRange(const QString& clusterName, const QString& scenarioName, const QString& positionName, double stackBb) {
    Q_UNUSED(positionName);
    Q_UNUSED(stackBb);

    QSqlDatabase d = db();
    if (!d.isOpen()) return "";

    QSqlQuery q(d);
    q.prepare("SELECT cr.range_counts "
              "FROM cluster_ranges cr "
              "JOIN profile_clusters c ON cr.id_cluster = c.id_cluster "
              "JOIN action_scenarios s ON cr.id_scenario = s.id_scenario "
              "WHERE c.cluster_name = :cname "
              "AND s.name = :sname");
    q.bindValue(":cname", clusterName);
    q.bindValue(":sname", scenarioName);

    if (!q.exec()) return "";

    QMap<QString, float> sumCountMap;
    int numRows = 0;

    while (q.next()) {
        QString countsTxt = q.value(0).toString();
        QStringList countParts = countsTxt.split(",");
        for (const QString& part : countParts) {
            QStringList kv = part.split(":");
            if (kv.size() >= 2) {
                sumCountMap[kv[0]] += kv[1].toFloat();
            }
        }
        numRows++;
    }

    if (numRows == 0 || sumCountMap.isEmpty()) return "";

    // Calculate ceiling count for clean filter
    QList<float> countsList = sumCountMap.values();
    std::sort(countsList.begin(), countsList.end(), std::greater<float>());
    float ceilingCount = 0.0f;
    int ceilingItems = std::min(5, (int)countsList.size());
    for (int i = 0; i < ceilingItems; ++i) {
        ceilingCount += countsList[i];
    }
    if (ceilingItems > 0) ceilingCount /= ceilingItems;
    if (ceilingCount < 1.0f) ceilingCount = 1.0f;

    QStringList resultParts;
    for (auto it = sumCountMap.begin(); it != sumCountMap.end(); ++it) {
        float sum_c = it.value();
        // The Clean Range filter: drop anything below 20% of the ceiling count
        if (sum_c / ceilingCount >= 0.20f) {
            float final_prob = std::min(1.0f, sum_c / ceilingCount);
            if (final_prob > 0.001f) {
                resultParts << QString("%1:%2").arg(it.key()).arg(final_prob, 0, 'f', 4);
            }
        }
    }

    return resultParts.join(",");
}

bool DBManager::logGtoDecision(int handId, int playerId, const QString& street, 
                            const QString& actionSequence, const QString& actionTaken, 
                            const QString& gtoAction, double evAction, double evGto, double evLoss) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    QSqlQuery q(d);
    q.prepare("INSERT INTO player_gto_decisions (id_hand, id_player, street, action_sequence, "
              "action_taken, gto_action, ev_action, ev_gto, ev_loss) "
              "VALUES (:hid, :pid, :street, :seq, :taken, :gto, :ev_act, :ev_gto, :ev_loss)");
    q.bindValue(":hid", handId);
    q.bindValue(":pid", playerId);
    q.bindValue(":street", street);
    q.bindValue(":seq", actionSequence);
    q.bindValue(":taken", actionTaken);
    q.bindValue(":gto", gtoAction);
    q.bindValue(":ev_act", evAction);
    q.bindValue(":ev_gto", evGto);
    q.bindValue(":ev_loss", evLoss);

    if (!q.exec()) {
        qWarning() << "Failed to log GTO decision:" << q.lastError().text();
        return false;
    }
    return true;
}

int DBManager::saveSolve(const QVariant& handId, const QString& configText, double exploitability, const QByteArray& strategyBlob, bool is_node_locked, int parent_id_solve) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return -1;

    d.transaction();
    QSqlQuery q(d);
    q.prepare("INSERT INTO solves (id_hand, config_text, exploitability, is_node_locked, parent_id_solve, size_bytes) "
              "VALUES (:hid, :config, :exp, :locked, :parent, :size) "
              "RETURNING id_solve");
    q.bindValue(":hid", handId);
    q.bindValue(":config", configText);
    q.bindValue(":exp", exploitability);
    q.bindValue(":locked", is_node_locked);
    if (parent_id_solve >= 0) {
        q.bindValue(":parent", parent_id_solve);
    } else {
        q.bindValue(":parent", QVariant(QVariant::Int)); // null
    }
    q.bindValue(":size", static_cast<qint64>(strategyBlob.size()));

    if (!q.exec() || !q.next()) {
        qWarning() << "Failed to save solve:" << q.lastError().text();
        d.rollback();
        return -1;
    }
    
    int id_solve = q.value(0).toInt();
    int chunkSize = 10 * 1024 * 1024; // 10 MB chunks
    for (int i = 0; i < strategyBlob.size(); i += chunkSize) {
        QByteArray chunk = strategyBlob.mid(i, chunkSize);
        QSqlQuery qc(d);
        qc.prepare("INSERT INTO solve_chunks (id_solve, chunk_idx, chunk_data) VALUES (:sid, :idx, :data)");
        qc.bindValue(":sid", id_solve);
        qc.bindValue(":idx", i / chunkSize);
        qc.bindValue(":data", chunk);
        if (!qc.exec()) {
            qWarning() << "Failed to save solve chunk:" << qc.lastError().text();
            d.rollback();
            return -1;
        }
    }
    
    d.commit();
    return id_solve;
}

bool DBManager::loadSolveById(int solveId, QString& configText, double& exploitability, QByteArray& strategyBlob) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    QSqlQuery q(d);
    q.prepare("SELECT config_text, exploitability FROM solves WHERE id_solve = :sid");
    q.bindValue(":sid", solveId);
    
    if (q.exec() && q.next()) {
        configText = q.value(0).toString();
        exploitability = q.value(1).toDouble();
        
        QSqlQuery qc(d);
        qc.prepare("SELECT chunk_data FROM solve_chunks WHERE id_solve = :sid ORDER BY chunk_idx ASC");
        qc.bindValue(":sid", solveId);
        if (!qc.exec()) {
            qWarning() << "Failed to load solve chunks. Error:" << qc.lastError().text();
            return false;
        }
        
        strategyBlob.clear();
        while (qc.next()) {
            strategyBlob.append(qc.value(0).toByteArray());
        }
        
        if (strategyBlob.isEmpty()) {
            qWarning() << "Solve loaded but no binary chunks were found!";
            return false;
        }
        
        return true;
    }
    qWarning() << "Failed to load solve metadata from DB. Error:" << q.lastError().text();
    return false;
}

bool DBManager::deleteSolve(int solveId) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    QSqlQuery q(d);
    q.prepare("DELETE FROM solves WHERE id_solve = :sid");
    q.bindValue(":sid", solveId);
    if (q.exec()) {
        return true;
    }
    qWarning() << "Failed to delete solve. Error:" << q.lastError().text();
    return false;
}

QSqlQueryModel* DBManager::getSolvesModel(QObject* parent) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return nullptr;

    QSqlQueryModel* model = new QSqlQueryModel(parent);
    model->setQuery("SELECT id_solve as \"Solve ID\", id_hand as \"Hand ID\", solve_date as \"Date\", "
                    "round(exploitability::numeric, 4) as \"Exp\", "
                    "round(size_bytes::numeric / 1048576.0, 2) || ' MB' as \"Size\", "
                    "substring(config_text from 'set_board ([^\\n]+)') as \"Board\" "
                    "FROM solves ORDER BY solve_date DESC", d);
    return model;
}

int DBManager::saveParameter(const QString& name, const QString& text) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return -1;

    QSqlQuery q(d);
    q.prepare("INSERT INTO saved_parameters (param_name, param_text) VALUES (:name, :text) "
              "ON CONFLICT (param_name) DO UPDATE SET param_text = EXCLUDED.param_text, created_date = CURRENT_TIMESTAMP "
              "RETURNING id_param");
    q.bindValue(":name", name);
    q.bindValue(":text", text);
    
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    qWarning() << "Failed to save parameter:" << q.lastError().text();
    return -1;
}

bool DBManager::loadParameter(int id, QString& text) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    QSqlQuery q(d);
    q.prepare("SELECT param_text FROM saved_parameters WHERE id_param = :id");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        text = q.value(0).toString();
        return true;
    }
    return false;
}

bool DBManager::deleteParameter(int id) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    QSqlQuery q(d);
    q.prepare("DELETE FROM saved_parameters WHERE id_param = :id");
    q.bindValue(":id", id);
    if (q.exec()) {
        return true;
    }
    return false;
}

QSqlQueryModel* DBManager::getParametersModel(QObject* parent) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return nullptr;

    QSqlQueryModel* model = new QSqlQueryModel(parent);
    model->setQuery("SELECT id_param as \"ID\", param_name as \"Parameter Name\", created_date as \"Saved Date\" "
                    "FROM saved_parameters ORDER BY created_date DESC", d);
    return model;
}

bool DBManager::updateTrainingSample(int row_id, const std::vector<float>& oop_range, const std::vector<float>& ip_range, const std::vector<float>& oop_evs, const std::vector<float>& ip_evs, float exploitability, float solve_duration_sec) {
    QSqlDatabase d = db();
    if (!d.isOpen()) return false;

    QSqlQuery q(d);
    
    // Convert std::vector to PostgreSQL array string format: {1.0, 2.0, ...}
    auto vectorToArrayStr = [](const std::vector<float>& vec) -> QString {
        QStringList list;
        for (float val : vec) {
            list << QString::number(val, 'g', 6);
        }
        return "{" + list.join(",") + "}";
    };

    QString oopRangeStr = vectorToArrayStr(oop_range);
    QString ipRangeStr = vectorToArrayStr(ip_range);
    QString oopEvsStr = vectorToArrayStr(oop_evs);
    QString ipEvsStr = vectorToArrayStr(ip_evs);

    q.prepare("UPDATE training_samples SET "
              "oop_range = :oop_r::real[], ip_range = :ip_r::real[], "
              "oop_evs = :oop_e::real[], ip_evs = :ip_e::real[], exploitability = :expl, "
              "solve_duration_sec = :dur, status = 'solved' "
              "WHERE id = :id");

    q.bindValue(":oop_r", oopRangeStr);
    q.bindValue(":ip_r", ipRangeStr);
    q.bindValue(":oop_e", oopEvsStr);
    q.bindValue(":ip_e", ipEvsStr);
    q.bindValue(":expl", exploitability);
    q.bindValue(":dur", solve_duration_sec);
    q.bindValue(":id", row_id);

    if (!q.exec()) {
        QString err = q.lastError().text();
        qWarning() << "Failed to update training sample:" << err;
        std::cerr << "Failed to update training sample: " << err.toStdString() << std::endl;
        return false;
    }
    std::cout << "Successfully updated training sample row! Rows affected: " << q.numRowsAffected() << std::endl;
    return true;
}
