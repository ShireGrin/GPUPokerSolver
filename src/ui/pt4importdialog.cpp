#include "include/ui/pt4importdialog.h"
#include "ui_pt4importdialog.h"
#include "include/ui/handvisualizerwidget.h"
#include "include/tools/PrivateRangeConverter.h"
#include "include/tools/dbmanager.h"
#include <QSplitter>
#include <QMessageBox>
#include <QComboBox>
#include <QTimer>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QSet>
#include <QGuiApplication>
#include <QClipboard>

PT4ImportDialog::PT4ImportDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PT4ImportDialog),
    m_hasSelectedHand(false),
    m_isPreflopImport(false),
    m_isSelectionLocked(false),
    m_lockedRow(-1),
    m_currentPage(1)
{
    ui->setupUi(this);

    // --- NEW CODE: Dynamic Database Dropdown ---
    // Clear any hardcoded databases from the .ui file
    ui->databaseComboBox->clear();

    // Read the comma-separated list from the environment (with your current defaults)
    QString dbEnvString = qEnvironmentVariable("PT4_IMPORT_DATABASES", "DBNAME");
    
    // Split the string by commas and add each one to the dropdown
    QStringList dbList = dbEnvString.split(",", Qt::SkipEmptyParts);
    for (const QString& dbName : dbList) {
        ui->databaseComboBox->addItem(dbName.trimmed());
    }
    // -------------------------------------------

    // Make dialog standalone, resizable, and maximizable
    setWindowFlags(windowFlags() | Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    // Auto-maximize dialog on start
    QTimer::singleShot(50, this, [this]() {
        this->setWindowState(this->windowState() | Qt::WindowMaximized);
    });

    // Setup columns
    ui->handsTable->setColumnCount(9);
    QStringList headers;
    headers << "Date" << "Hand #" << "Board" << "Pot" << "Eff. Stack" << "IP Player" << "OOP Player" << "Turn" << "River";
    ui->handsTable->setHorizontalHeaderLabels(headers);
    ui->handsTable->horizontalHeader()->setStretchLastSection(true);
    
    setupDatabase();
    loadProfiles();

    // Default dates
    ui->endDateEdit->setDate(QDate::currentDate());
    ui->startDateEdit->setDate(QDate::currentDate().addDays(-90));
    ui->dateFilterCheckBox->setChecked(false);
    ui->startDateEdit->setEnabled(false);
    ui->endDateEdit->setEnabled(false);

    // Default pagination state
    ui->prevPageButton->setEnabled(false);
    ui->nextPageButton->setEnabled(false);
    ui->pageLabel->setText("Page 1");

    connect(ui->searchButton, &QPushButton::clicked, this, &PT4ImportDialog::on_searchButton_clicked);
    connect(ui->importButton, &QPushButton::clicked, this, &PT4ImportDialog::on_importButton_clicked);
    connect(ui->prevPageButton, &QPushButton::clicked, this, &PT4ImportDialog::prevPage);
    connect(ui->nextPageButton, &QPushButton::clicked, this, &PT4ImportDialog::nextPage);

    connect(ui->dateFilterCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        ui->startDateEdit->setEnabled(checked);
        ui->endDateEdit->setEnabled(checked);
    });

    connect(ui->databaseComboBox, &QComboBox::currentTextChanged, this, [this](const QString &) {
        setupDatabase();
        on_searchButton_clicked();
    });

    ui->playerSearchEdit->setText(qEnvironmentVariable("DEFAULT_PLAYERS", "PlayerNickname1, PlayerNickName2"));

    // Programmatic UI layout modification to add HandVisualizerWidget side-by-side
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    ui->verticalLayout->removeWidget(ui->handsTable);
    leftLayout->addWidget(ui->handsTable);
    
    ui->verticalLayout->removeItem(ui->paginationLayout);
    leftLayout->addLayout(ui->paginationLayout);
    
    splitter->addWidget(leftPanel);
    
    visualizer = new HandVisualizerWidget(this);
    splitter->addWidget(visualizer);
    
    ui->verticalLayout->insertWidget(1, splitter);
    
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    this->resize(1100, 650);

    // Connect selection change signal to update visualizer
    connect(ui->handsTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &PT4ImportDialog::onHandSelectionChanged);

    // Enable hover detection on the table
    ui->handsTable->setMouseTracking(true);
    connect(ui->handsTable, &QTableWidget::cellEntered, this, &PT4ImportDialog::onHandHovered);

    // Lock selection when hand is explicitly clicked (or toggle it off if clicked again)
    connect(ui->handsTable, &QTableWidget::cellClicked, this, &PT4ImportDialog::onHandClicked);

    // Add "Copy Raw Hand" button programmatically
    copyRawButton = new QPushButton("Copy Raw Hand", this);
    copyRawButton->setEnabled(false);
    ui->horizontalLayout_2->insertWidget(3, copyRawButton);

    connect(copyRawButton, &QPushButton::clicked, this, [this]() {
        if (!m_currentHistoryText.isEmpty()) {
            QGuiApplication::clipboard()->setText(m_currentHistoryText);
            // Visual feedback
            QString oldText = copyRawButton->text();
            copyRawButton->setText("Copied!");
            copyRawButton->setEnabled(false);
            QTimer::singleShot(1000, this, [this, oldText]() {
                copyRawButton->setText(oldText);
                copyRawButton->setEnabled(!m_currentHistoryText.isEmpty());
            });
        }
    });

    QTimer::singleShot(10, this, [this](){ on_searchButton_clicked(); });
}

PT4ImportDialog::~PT4ImportDialog()
{
    delete ui;
}

bool PT4ImportDialog::hasSelectedHand() const
{
    return m_hasSelectedHand;
}

ImportedHand PT4ImportDialog::getSelectedHand() const
{
    return m_selectedHand;
}

void PT4ImportDialog::setupDatabase()
{
    QString dbName = qEnvironmentVariable("DB_PT4", "PT4");
    if (ui && ui->databaseComboBox) {
        dbName = ui->databaseComboBox->currentText();
    }

    if (QSqlDatabase::contains("pt4_conn")) {
        db = QSqlDatabase::database("pt4_conn");
        if (db.isOpen()) {
            db.close();
        }
    } else {
        db = QSqlDatabase::addDatabase("QPSQL", "pt4_conn");
    }
    
    db.setHostName(qEnvironmentVariable("DB_PT4_HOST", "localhost"));
    db.setPort(qEnvironmentVariable("DB_PT4_PORT", "5432").toInt());
    db.setDatabaseName(dbName);
    db.setUserName(qEnvironmentVariable("DB_PT4_USER", "postgres"));
    db.setPassword(qEnvironmentVariable("DB_PT4_PASSWORD", "dbpass"));
    
    if (!db.open()) {
        QMessageBox::critical(this, "Database Error", "Failed to connect to Database (" + dbName + "): " + db.lastError().text());
    }
}

void PT4ImportDialog::loadProfiles()
{
    QString profilePath = QCoreApplication::applicationDirPath() + "/../scripts/profiles.json";
    QFile file(profilePath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        QJsonDocument doc(QJsonDocument::fromJson(data));
        QJsonObject root = doc.object();
        profilesData = root["profiles"].toObject();
        
        QJsonObject mapping = root["player_mapping"].toObject();
        for (auto it = mapping.begin(); it != mapping.end(); ++it) {
            playerMapping[it.key()] = it.value().toString();
        }
    } else {
        qDebug() << "Failed to open profiles.json at" << profilePath;
    }
}

// Map card int to string (PT4 card integers: 1=2c, 2=2d, 3=2h, 4=2s ... 52=As)
QString cardIntToString(int c) {
    if (c <= 0 || c > 52) return "";
    QString ranks = "23456789TJQKA";
    QString suits = "cdhs";
    int rankIdx = (c - 1) % 13;
    int suitIdx = (c - 1) / 13;
    return QString("%1%2").arg(ranks[rankIdx]).arg(suits[suitIdx]);
}

void PT4ImportDialog::on_searchButton_clicked()
{
    m_currentPage = 1;
    runSearch();
}

void PT4ImportDialog::prevPage()
{
    if (m_currentPage > 1) {
        m_currentPage--;
        runSearch();
    }
}

void PT4ImportDialog::nextPage()
{
    m_currentPage++;
    runSearch();
}

void PT4ImportDialog::runSearch()
{
    QString playerInput = ui->playerSearchEdit->text().trimmed();
    QString handNoInput = ui->handNoSearchEdit->text().trimmed().replace("#", "");

    if (playerInput.isEmpty() && handNoInput.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a player name or hand number.");
        return;
    }

    ui->handsTable->setSortingEnabled(false);
    ui->handsTable->setRowCount(0);
    ui->handsTable->setColumnCount(9);
    QStringList headers;
    headers << "Date" << "Hand #" << "Board" << "Pot" << "Eff. Stack" << "IP Player" << "OOP Player" << "Turn" << "River";
    ui->handsTable->setHorizontalHeaderLabels(headers);
    ui->handsTable->horizontalHeader()->setStretchLastSection(true);

    QStringList whereClauses;
    if (ui->flopFilterCheckBox->isChecked()) {
        whereClauses << "s.cnt_players_f >= 2" << "ps.flg_f_saw = true";
    }

    if (!playerInput.isEmpty()) {
        QStringList players = playerInput.split(",", Qt::SkipEmptyParts);
        QStringList playerConditions;
        for(const QString& p : players) {
            playerConditions << QString("'%1'").arg(p.trimmed().replace("'", "''"));
        }
        whereClauses << QString("p.player_name IN (%1)").arg(playerConditions.join(","));
    }

    if (!handNoInput.isEmpty()) {
        whereClauses << "CAST(s.hand_no AS text) LIKE :hand_no_pattern";
    }

    if (ui->dateFilterCheckBox->isChecked()) {
        whereClauses << "s.date_played >= :start_date" << "s.date_played <= :end_date";
    }

    QString whereString = whereClauses.join(" AND ");

    int queryLimit = m_pageSize + 1;
    int queryOffset = (m_currentPage - 1) * m_pageSize;

    QSqlQuery q(db);
    q.prepare(QString("SELECT "
                      "s.id_hand, s.date_played, s.hand_no, s.cnt_players, "
                      "s.amt_pot_f, s.amt_pot_t, s.amt_pot_r, "
                      "ps.amt_f_effective_stack, ps.amt_t_effective_stack, ps.amt_r_effective_stack, "
                      "s.card_1, s.card_2, s.card_3, s.card_4, s.card_5, b.amt_bb, ps.amt_ante "
                      "FROM tourney_hand_summary s "
                      "JOIN tourney_hand_player_statistics ps ON s.id_hand = ps.id_hand "
                      "JOIN player p ON ps.id_player = p.id_player "
                      "JOIN tourney_blinds b ON s.id_blinds = b.id_blinds "
                      "WHERE %1 "
                      "ORDER BY s.date_played DESC LIMIT %2 OFFSET %3").arg(whereString).arg(queryLimit).arg(queryOffset));

    if (!handNoInput.isEmpty()) {
        q.bindValue(":hand_no_pattern", "%" + handNoInput + "%");
    }

    if (ui->dateFilterCheckBox->isChecked()) {
        QDateTime startDateTime(ui->startDateEdit->date(), QTime(0, 0, 0));
        QDateTime endDateTime(ui->endDateEdit->date(), QTime(23, 59, 59));
        q.bindValue(":start_date", startDateTime);
        q.bindValue(":end_date", endDateTime);
    }

    if (!q.exec()) {
        QMessageBox::critical(this, "Query Error", q.lastError().text());
        ui->handsTable->setSortingEnabled(true);
        return;
    }

    int row = 0;
    bool hasNextPage = false;
    while (q.next()) {
        if (row >= m_pageSize) {
            hasNextPage = true;
            break;
        }

        int id_hand = q.value("id_hand").toInt();
        QString date_played = q.value("date_played").toString();
        QString hand_no = q.value("hand_no").toString();

        int turn_c = q.value("card_4").toInt();
        int river_c = q.value("card_5").toInt();
        QString turn_str = turn_c > 0 ? cardIntToString(turn_c) : "";
        QString river_str = river_c > 0 ? cardIntToString(river_c) : "";

        float pot = q.value("amt_pot_f").toFloat();
        float eff_stack = q.value("amt_f_effective_stack").toFloat();

        float amt_bb = q.value("amt_bb").toFloat();
        if (amt_bb > 0) {
            pot /= amt_bb;
            eff_stack /= amt_bb;
        }

        QStringList boardCards;
        boardCards << cardIntToString(q.value("card_1").toInt())
                   << cardIntToString(q.value("card_2").toInt())
                   << cardIntToString(q.value("card_3").toInt());

        QString board = boardCards.join(",");

        // Find players on the turn if it went to turn, otherwise on the flop
        QSqlQuery qPlayers(db);
        if (turn_c > 0) {
            qPlayers.prepare("SELECT p.player_name, ps.position "
                             "FROM tourney_hand_player_statistics ps "
                             "JOIN player p ON ps.id_player = p.id_player "
                             "WHERE ps.id_hand = :hid AND ps.flg_t_saw = true "
                             "ORDER BY ps.position ASC");
        } else {
            qPlayers.prepare("SELECT p.player_name, ps.position "
                             "FROM tourney_hand_player_statistics ps "
                             "JOIN player p ON ps.id_player = p.id_player "
                             "WHERE ps.id_hand = :hid AND ps.flg_f_saw = true "
                             "ORDER BY ps.position ASC");
        }
        qPlayers.bindValue(":hid", id_hand);

        QString ipPlayer = "Unknown";
        QString oopPlayer = "Unknown";

        if (qPlayers.exec()) {
            QStringList players;
            QList<int> positions;
            while(qPlayers.next()) {
                players << qPlayers.value("player_name").toString();
                positions << qPlayers.value("position").toInt();
            }
            if (players.size() >= 2) {
                oopPlayer = players[players.size()-1];
                ipPlayer = players[0];
            }
        }

        QString ipCluster = DBManager::instance().getClusterForPlayerName(ipPlayer);
        QString oopCluster = DBManager::instance().getClusterForPlayerName(oopPlayer);
        
        QString ipDisplay = QString("%1 (%2)").arg(ipPlayer).arg(ipCluster);
        QString oopDisplay = QString("%1 (%2)").arg(oopPlayer).arg(oopCluster);

        ui->handsTable->insertRow(row);
        ui->handsTable->setItem(row, 0, new QTableWidgetItem(date_played));
        ui->handsTable->setItem(row, 1, new QTableWidgetItem(hand_no));
        ui->handsTable->setItem(row, 2, new QTableWidgetItem(board));
        ui->handsTable->setItem(row, 3, new QTableWidgetItem(QString::number(pot, 'f', 2)));
        ui->handsTable->setItem(row, 4, new QTableWidgetItem(QString::number(eff_stack, 'f', 2)));
        QTableWidgetItem* ipItem = new QTableWidgetItem(ipDisplay);
        ipItem->setData(Qt::UserRole, ipPlayer);
        ui->handsTable->setItem(row, 5, ipItem);
        
        QTableWidgetItem* oopItem = new QTableWidgetItem(oopDisplay);
        oopItem->setData(Qt::UserRole, oopPlayer);
        ui->handsTable->setItem(row, 6, oopItem);
        
        ui->handsTable->setItem(row, 7, new QTableWidgetItem(turn_str));
        ui->handsTable->setItem(row, 8, new QTableWidgetItem(river_str));

        ui->handsTable->item(row, 0)->setData(Qt::UserRole, id_hand);
        ui->handsTable->item(row, 1)->setData(Qt::UserRole, q.value("cnt_players").toInt());
        ui->handsTable->item(row, 3)->setData(Qt::UserRole, q.value("amt_bb").toFloat());

        row++;
    }

    ui->handsTable->setSortingEnabled(true);

    ui->prevPageButton->setEnabled(m_currentPage > 1);
    ui->nextPageButton->setEnabled(hasNextPage);
    ui->pageLabel->setText(QString("Page %1").arg(m_currentPage));
}

void PT4ImportDialog::getProfileData(const QString& playerName, int position, const QString& scenarioName, const QString& positionName, double stackBb, double blindLevel, ParsedProfileData& pd) const
{
    // Fetch profile from new standalone DB using player name, stack size, and blind level
    QJsonObject prof = DBManager::instance().getProfileForPlayerByName(playerName, 1, stackBb, blindLevel);

    // Default fallback values
    QString base_bet_sizes = "33 50";
    QString base_raise_sizes = "50";

    pd.flop_bet_sizes = (position == 1) ? "" : base_bet_sizes;
    pd.turn_bet_sizes = base_bet_sizes;
    pd.river_bet_sizes = base_bet_sizes;

    pd.flop_raise_sizes = base_raise_sizes;
    pd.turn_raise_sizes = base_raise_sizes;
    pd.river_raise_sizes = base_raise_sizes;

    pd.turn_donk_sizes = (position == 1) ? "33 70" : "";
    pd.river_donk_sizes = (position == 1) ? "50" : "";

    pd.flop_allin = false;
    pd.turn_allin = false;
    pd.river_allin = true;

    QString rangeVal = "";

    if (!prof.isEmpty()) {
        base_bet_sizes = prof.value("bet_sizes").toString("33 50");
        base_raise_sizes = prof.value("raise_sizes").toString("50");

        QString posPrefix = (position == 1) ? "oop" : "ip";

        // Bet sizes
        pd.flop_bet_sizes = prof.value(QString("flop_%1_bet_sizes").arg(posPrefix)).toString((position == 1) ? "" : base_bet_sizes);
        pd.turn_bet_sizes = prof.value(QString("turn_%1_bet_sizes").arg(posPrefix)).toString(base_bet_sizes);
        pd.river_bet_sizes = prof.value(QString("river_%1_bet_sizes").arg(posPrefix)).toString(base_bet_sizes);

        // Raise sizes
        pd.flop_raise_sizes = prof.value(QString("flop_%1_raise_sizes").arg(posPrefix)).toString(base_raise_sizes);
        pd.turn_raise_sizes = prof.value(QString("turn_%1_raise_sizes").arg(posPrefix)).toString(base_raise_sizes);
        pd.river_raise_sizes = prof.value(QString("river_%1_raise_sizes").arg(posPrefix)).toString(base_raise_sizes);

        // Donk sizes
        pd.turn_donk_sizes = prof.value(QString("turn_%1_donk_sizes").arg(posPrefix)).toString((position == 1) ? "33 70" : "");
        pd.river_donk_sizes = prof.value(QString("river_%1_donk_sizes").arg(posPrefix)).toString((position == 1) ? "50" : "");

        // All-in flags
        pd.flop_allin = prof.value(QString("flop_%1_allin").arg(posPrefix)).toBool(false);
        pd.turn_allin = prof.value(QString("turn_%1_allin").arg(posPrefix)).toBool(false);
        pd.river_allin = prof.value(QString("river_%1_allin").arg(posPrefix)).toBool(true);

        if (prof.contains("ranges")) {
            QJsonObject rangesObj = prof["ranges"].toObject();
            QString rangeKey = "SRP_call";
            if (scenarioName == "RFI" || scenarioName == "open_jam" || scenarioName == "isolate") rangeKey = "SRP_raise";
            else if (scenarioName == "3bet_vs_pfr" || scenarioName == "3bet_jam" || scenarioName == "4bet_vs_3bet") rangeKey = "3BP_raise";
            else if (scenarioName == "call_vs_3bet") rangeKey = "3BP_call";
            
            if (rangesObj.contains(rangeKey)) {
                rangeVal = rangesObj[rangeKey].toString();
            } else {
                // Fallback keys if the SRP/3BP keys are missing
                if (rangeKey == "3BP_raise") {
                    rangeVal = rangesObj.contains("SRP_raise") ? rangesObj.value("SRP_raise").toString() : rangesObj.value("BTN").toString();
                } else if (rangeKey == "3BP_call") {
                    rangeVal = rangesObj.contains("SRP_call") ? rangesObj.value("SRP_call").toString() : rangesObj.value("BB_defend").toString();
                } else if (rangeKey == "SRP_raise") {
                    rangeVal = rangesObj.value("BTN").toString();
                } else if (rangeKey == "SRP_call") {
                    rangeVal = rangesObj.value("BB_defend").toString();
                } else {
                    rangeVal = rangesObj.value("UTG").toString();
                }
            }
        }
    }

    QString clusterName = DBManager::instance().getClusterForPlayerName(playerName);
    if (clusterName == "Unknown" || clusterName.isEmpty()) {
        clusterName = "TAG (Tight-Aggressive)";
    }
    QString dataDrivenRange = DBManager::instance().getDataDrivenRange(clusterName, scenarioName, positionName, stackBb);

    if (!dataDrivenRange.isEmpty()) {
        pd.range = QString::fromStdString(expandPokerRange(dataDrivenRange.toStdString()));
    } else {
        if (rangeVal.isEmpty()) {
            rangeVal = "22+, A2s+, A2o+, K2s+, K5o+, Q5s+, Q9o+, J7s+, J9o+, T7s+, 97s+, 87s, 76s"; // Universal fallback
        }
        pd.range = QString::fromStdString(expandPokerRange(rangeVal.toStdString()));
    }
}

void PT4ImportDialog::getPreflopRangeKeys(int id_hand, const QString& ipPlayer, const QString& oopPlayer, QString& ipScenario, QString& oopScenario, QString& ipPosName, QString& oopPosName) const
{
    ipScenario = "All Scenarios";
    oopScenario = "All Scenarios";
    ipPosName = "BTN";
    oopPosName = "BB";

    QSqlQuery qPreflopAction(db);
    qPreflopAction.prepare("SELECT p.player_name, ps.position, ps.flg_p_first_raise, ps.flg_p_3bet, "
                           "ps.flg_p_4bet, ps.flg_p_ccall, ps.cnt_p_call, ps.flg_p_limp, ps.flg_p_open_opp, "
                           "ps.enum_allin, ps.enum_p_3bet_action, ps.flg_p_3bet_def_opp "
                           "FROM tourney_hand_player_statistics ps "
                           "JOIN player p ON ps.id_player = p.id_player "
                           "WHERE ps.id_hand = :hid AND p.player_name IN (:ip, :oop)");
    qPreflopAction.bindValue(":hid", id_hand);
    qPreflopAction.bindValue(":ip", ipPlayer);
    qPreflopAction.bindValue(":oop", oopPlayer);
    
    if (qPreflopAction.exec()) {
        while (qPreflopAction.next()) {
            QString name = qPreflopAction.value("player_name").toString().trimmed();
            int pos = qPreflopAction.value("position").toInt();
            bool rfi = qPreflopAction.value("flg_p_first_raise").toBool();
            bool threebet = qPreflopAction.value("flg_p_3bet").toBool();
            bool fourbet = qPreflopAction.value("flg_p_4bet").toBool();
            bool ccall = qPreflopAction.value("flg_p_ccall").toBool();
            int cnt_p_call = qPreflopAction.value("cnt_p_call").toInt();
            bool limp = qPreflopAction.value("flg_p_limp").toBool();
            bool open_opp = qPreflopAction.value("flg_p_open_opp").toBool();
            QString enum_allin = qPreflopAction.value("enum_allin").toString();
            QString enum_p_3bet_action = qPreflopAction.value("enum_p_3bet_action").toString();
            bool flg_p_3bet_def_opp = qPreflopAction.value("flg_p_3bet_def_opp").toBool();
            
            QString scen = "All Scenarios";
            if (rfi && open_opp) {
                scen = (enum_allin == "P") ? "open_jam" : "RFI";
            } else if (rfi && !open_opp) {
                scen = "isolate";
            } else if (threebet) {
                scen = (enum_allin == "P") ? "3bet_jam" : "3bet_vs_pfr";
            } else if (fourbet) {
                scen = "4bet_vs_3bet";
            } else if (flg_p_3bet_def_opp && enum_p_3bet_action == "C") {
                scen = "call_vs_3bet";
            } else if (ccall || (cnt_p_call > 0 && !limp)) {
                scen = "call_vs_pfr";
            } else if (limp) {
                scen = "limp";
            }
            
            QString posName = "UTG";
            if (pos == 0) posName = "BTN";
            else if (pos == 1) posName = "CO";
            else if (pos == 2) posName = "HJ";
            else if (pos == 3) posName = "LJ";
            else if (pos == 8) posName = "BB";
            else if (pos == 9) posName = "SB";

            if (name == ipPlayer.trimmed()) {
                ipScenario = scen;
                ipPosName = posName;
            } else if (name == oopPlayer.trimmed()) {
                oopScenario = scen;
                oopPosName = posName;
            }
        }
    }
}

void PT4ImportDialog::on_importButton_clicked()
{
    int row = ui->handsTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "Selection Error", "Please select a hand to import.");
        return;
    }
    
    int id_hand = ui->handsTable->item(row, 0)->data(Qt::UserRole).toInt();
    int cnt_players = ui->handsTable->item(row, 1)->data(Qt::UserRole).toInt();
    float amt_bb = ui->handsTable->item(row, 3)->data(Qt::UserRole).toFloat();
    if (amt_bb <= 0.0f) amt_bb = 2.0f; // Default fallback to 2
    
    m_selectedHand.cnt_players = cnt_players;
    m_selectedHand.bb_size = amt_bb;
    m_selectedHand.ante = 0.0f;
    m_selectedHand.hand_no = ui->handsTable->item(row, 1)->text();
    m_selectedHand.board = ui->handsTable->item(row, 2)->text();
    m_selectedHand.pot = ui->handsTable->item(row, 3)->text().toFloat();
    m_selectedHand.effective_stack = ui->handsTable->item(row, 4)->text().toFloat();
    m_selectedHand.ip_player = ui->handsTable->item(row, 5)->data(Qt::UserRole).toString();
    m_selectedHand.oop_player = ui->handsTable->item(row, 6)->data(Qt::UserRole).toString();
    m_selectedHand.turn_card = ui->handsTable->item(row, 7) ? ui->handsTable->item(row, 7)->text() : "";
    m_selectedHand.river_card = ui->handsTable->item(row, 8) ? ui->handsTable->item(row, 8)->text() : "";

    // Query tourney metadata from PT4 DB
    QString siteName = "PokerStars"; // fallback
    QString tourneyNo = "Unknown";
    double buyin = 0.0;
    QDateTime datePlayed = QDateTime::currentDateTime();

    QSqlQuery qTourney(db);
    qTourney.prepare("SELECT ls.site_name, ts.tourney_no, ts.amt_buyin, s.date_played "
                      "FROM tourney_hand_summary s "
                      "JOIN tourney_summary ts ON s.id_tourney = ts.id_tourney "
                      "JOIN lookup_sites ls ON ts.id_site = ls.id_site "
                      "WHERE s.id_hand = :hid");
    qTourney.bindValue(":hid", id_hand);
    if (qTourney.exec() && qTourney.next()) {
        siteName = qTourney.value("site_name").toString();
        tourneyNo = qTourney.value("tourney_no").toString();
        buyin = qTourney.value("amt_buyin").toDouble();
        datePlayed = qTourney.value("date_played").toDateTime();
    }

    // Mirror to STANDALONE database
    int dbSiteId = DBManager::instance().getOrCreateSite(siteName);
    int dbTourneyId = DBManager::instance().getOrCreateTourney(dbSiteId, tourneyNo, buyin, datePlayed);
    int dbIpPlayerId = DBManager::instance().getOrCreatePlayer(dbSiteId, m_selectedHand.ip_player);
    int dbOopPlayerId = DBManager::instance().getOrCreatePlayer(dbSiteId, m_selectedHand.oop_player);

    int dbHandId = DBManager::instance().getOrCreateHand(
        dbTourneyId, m_selectedHand.hand_no, datePlayed, cnt_players, amt_bb,
        m_selectedHand.ante, m_selectedHand.pot, m_selectedHand.effective_stack,
        m_selectedHand.board, dbIpPlayerId, dbOopPlayerId, m_selectedHand.turn_card, m_selectedHand.river_card
    );
    m_selectedHand.id_hand = dbHandId;

    QString ipScenario, oopScenario, ipPosName, oopPosName;
    getPreflopRangeKeys(id_hand, m_selectedHand.ip_player, m_selectedHand.oop_player, ipScenario, oopScenario, ipPosName, oopPosName);

    ParsedProfileData ip_pd;
    getProfileData(m_selectedHand.ip_player, 0, ipScenario, ipPosName, m_selectedHand.effective_stack, amt_bb, ip_pd);
    m_selectedHand.ip_range = ip_pd.range;
    m_selectedHand.ip_flop_bet_sizes = ip_pd.flop_bet_sizes;
    m_selectedHand.ip_turn_bet_sizes = ip_pd.turn_bet_sizes;
    m_selectedHand.ip_river_bet_sizes = ip_pd.river_bet_sizes;
    m_selectedHand.ip_flop_raise_sizes = ip_pd.flop_raise_sizes;
    m_selectedHand.ip_turn_raise_sizes = ip_pd.turn_raise_sizes;
    m_selectedHand.ip_river_raise_sizes = ip_pd.river_raise_sizes;
    m_selectedHand.ip_flop_allin = ip_pd.flop_allin;
    m_selectedHand.ip_turn_allin = ip_pd.turn_allin;
    m_selectedHand.ip_river_allin = ip_pd.river_allin;

    ParsedProfileData oop_pd;
    getProfileData(m_selectedHand.oop_player, 1, oopScenario, oopPosName, m_selectedHand.effective_stack, amt_bb, oop_pd);
    m_selectedHand.oop_range = oop_pd.range;
    m_selectedHand.oop_flop_bet_sizes = oop_pd.flop_bet_sizes;
    m_selectedHand.oop_turn_bet_sizes = oop_pd.turn_bet_sizes;
    m_selectedHand.oop_river_bet_sizes = oop_pd.river_bet_sizes;
    m_selectedHand.oop_flop_raise_sizes = oop_pd.flop_raise_sizes;
    m_selectedHand.oop_turn_raise_sizes = oop_pd.turn_raise_sizes;
    m_selectedHand.oop_river_raise_sizes = oop_pd.river_raise_sizes;
    m_selectedHand.oop_turn_donk_sizes = oop_pd.turn_donk_sizes;
    m_selectedHand.oop_river_donk_sizes = oop_pd.river_donk_sizes;
    m_selectedHand.oop_flop_allin = oop_pd.flop_allin;
    m_selectedHand.oop_turn_allin = oop_pd.turn_allin;
    m_selectedHand.oop_river_allin = oop_pd.river_allin;

    // Query all players dealt into the hand for preflop solver configuration
    m_selectedHand.preflop_players.clear();
    QSqlQuery qPlayers(db);
    qPlayers.prepare("SELECT p.player_name, ps.position, ps.amt_before, ps.amt_ante, "
                     "ps.flg_p_fold, ps.cnt_p_call, ps.cnt_p_raise, "
                     "ps.flg_p_first_raise, ps.flg_p_3bet, ps.flg_p_4bet, ps.flg_p_ccall, "
                     "ps.flg_p_limp, ps.flg_p_open_opp, ps.enum_allin, ps.enum_p_3bet_action, ps.flg_p_3bet_def_opp "
                     "FROM tourney_hand_player_statistics ps "
                     "JOIN player p ON ps.id_player = p.id_player "
                     "WHERE ps.id_hand = :hid "
                     "ORDER BY ps.position ASC");
    qPlayers.bindValue(":hid", id_hand);
    if (qPlayers.exec()) {
        bool anteSet = false;
        QSet<QString> seenNames;
        while (qPlayers.next()) {
            PreflopPlayer player;
            player.name = qPlayers.value("player_name").toString().trimmed();
            if (seenNames.contains(player.name)) {
                continue;
            }
            seenNames.insert(player.name);
            player.position = qPlayers.value("position").toInt();
            float amt_before = qPlayers.value("amt_before").toFloat();
            player.stack = amt_before / amt_bb;
            player.folded = qPlayers.value("flg_p_fold").toBool();
            player.calls = qPlayers.value("cnt_p_call").toInt();
            player.raises = qPlayers.value("cnt_p_raise").toInt();

            // Capture ante from first player (same for all players in the hand)
            if (!anteSet) {
                float raw_ante = qPlayers.value("amt_ante").toFloat();
                m_selectedHand.ante = raw_ante / amt_bb;
                anteSet = true;
            }

            ParsedProfileData pd;
            QString posName = "UTG";
            if (player.position == 0) posName = "BTN";
            else if (player.position == 1) posName = "CO";
            else if (player.position == 2) posName = "HJ";
            else if (player.position == 3) posName = "LJ";
            else if (player.position == 8) posName = "BB";
            else if (player.position == 9) posName = "SB";

            bool rfi = qPlayers.value("flg_p_first_raise").toBool();
            bool threebet = qPlayers.value("flg_p_3bet").toBool();
            bool fourbet = qPlayers.value("flg_p_4bet").toBool();
            bool ccall = qPlayers.value("flg_p_ccall").toBool();
            bool limp = qPlayers.value("flg_p_limp").toBool();
            bool open_opp = qPlayers.value("flg_p_open_opp").toBool();
            QString enum_allin = qPlayers.value("enum_allin").toString();
            QString enum_p_3bet_action = qPlayers.value("enum_p_3bet_action").toString();
            bool flg_p_3bet_def_opp = qPlayers.value("flg_p_3bet_def_opp").toBool();
            
            QString scen = "All Scenarios";
            if (rfi && open_opp) {
                scen = (enum_allin == "P") ? "open_jam" : "RFI";
            } else if (rfi && !open_opp) {
                scen = "isolate";
            } else if (threebet) {
                scen = (enum_allin == "P") ? "3bet_jam" : "3bet_vs_pfr";
            } else if (fourbet) {
                scen = "4bet_vs_3bet";
            } else if (flg_p_3bet_def_opp && enum_p_3bet_action == "C") {
                scen = "call_vs_3bet";
            } else if (ccall || (player.calls > 0 && !limp)) {
                scen = "call_vs_pfr";
            } else if (limp) {
                scen = "limp";
            }

            getProfileData(player.name, player.position, scen, posName, player.stack, amt_bb, pd);
            player.range = pd.range;

            m_selectedHand.preflop_players.push_back(player);
        }
    }
    
    m_hasSelectedHand = true;
    accept();
}

#include <QApplication>
#include <QClipboard>

void PT4ImportDialog::on_copyParamsButton_clicked()
{
    int row = ui->handsTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "Selection Error", "Please select a hand first.");
        return;
    }
    
    int id_hand = ui->handsTable->item(row, 0)->data(Qt::UserRole).toInt();
    QString board = ui->handsTable->item(row, 2)->text();
    float pot = ui->handsTable->item(row, 3)->text().toFloat();
    float effective_stack = ui->handsTable->item(row, 4)->text().toFloat();
    QString ip_player = ui->handsTable->item(row, 5)->data(Qt::UserRole).toString();
    QString oop_player = ui->handsTable->item(row, 6)->data(Qt::UserRole).toString();
    QString turn_card = ui->handsTable->item(row, 7) ? ui->handsTable->item(row, 7)->text() : "";
    QString river_card = ui->handsTable->item(row, 8) ? ui->handsTable->item(row, 8)->text() : "";
    
    QString ipScenario, oopScenario, ipPosName, oopPosName;
    getPreflopRangeKeys(id_hand, ip_player, oop_player, ipScenario, oopScenario, ipPosName, oopPosName);

    ParsedProfileData ip_pd;
    getProfileData(ip_player, 0, ipScenario, ipPosName, effective_stack, 1.0, ip_pd);

    ParsedProfileData oop_pd;
    getProfileData(oop_player, 1, oopScenario, oopPosName, effective_stack, 1.0, oop_pd);

    QString output_text;
    QTextStream out(&output_text);
    out << "set_pot " << pot << "\n";
    out << "set_effective_stack " << effective_stack << "\n";
    out << "set_board " << board << "\n";
    
    if (!turn_card.isEmpty() || !river_card.isEmpty()) {
        out << "set_preselected_cards ";
        if (!turn_card.isEmpty()) out << turn_card;
        if (!river_card.isEmpty()) out << "," << river_card;
        out << "\n";
    }

    out << "set_range_oop " << oop_pd.range << "\n";
    out << "set_range_ip " << ip_pd.range << "\n";

    auto write_bet_sizes = [&](const QString& p_str, const QString& phase_str, const QString& type_str, const QString& sizes) {
        if (!sizes.isEmpty()) {
            out << "set_bet_sizes " << p_str << "," << phase_str << "," << type_str << "," << QString(sizes).replace(' ', ',') << "\n";
        }
    };
    auto write_allin = [&](const QString& p_str, const QString& phase_str, bool allin) {
        if (allin) out << "set_bet_sizes " << p_str << "," << phase_str << ",allin\n";
    };

    // Flop OOP
    write_bet_sizes("oop", "flop", "bet", oop_pd.flop_bet_sizes);
    write_bet_sizes("oop", "flop", "raise", oop_pd.flop_raise_sizes);
    write_allin("oop", "flop", oop_pd.flop_allin);
    
    // Flop IP
    write_bet_sizes("ip", "flop", "bet", ip_pd.flop_bet_sizes);
    write_bet_sizes("ip", "flop", "raise", ip_pd.flop_raise_sizes);
    write_allin("ip", "flop", ip_pd.flop_allin);

    // Turn OOP
    write_bet_sizes("oop", "turn", "bet", oop_pd.turn_bet_sizes);
    write_bet_sizes("oop", "turn", "raise", oop_pd.turn_raise_sizes);
    write_bet_sizes("oop", "turn", "donk", oop_pd.turn_donk_sizes);
    write_allin("oop", "turn", oop_pd.turn_allin);

    // Turn IP
    write_bet_sizes("ip", "turn", "bet", ip_pd.turn_bet_sizes);
    write_bet_sizes("ip", "turn", "raise", ip_pd.turn_raise_sizes);
    write_allin("ip", "turn", ip_pd.turn_allin);

    // River OOP
    write_bet_sizes("oop", "river", "bet", oop_pd.river_bet_sizes);
    write_bet_sizes("oop", "river", "raise", oop_pd.river_raise_sizes);
    write_bet_sizes("oop", "river", "donk", oop_pd.river_donk_sizes);
    write_allin("oop", "river", oop_pd.river_allin);

    // River IP
    write_bet_sizes("ip", "river", "bet", ip_pd.river_bet_sizes);
    write_bet_sizes("ip", "river", "raise", ip_pd.river_raise_sizes);
    write_allin("ip", "river", ip_pd.river_allin);

    out << "set_allin_threshold 0.67\n";
    out << "set_raise_limit 3\n";
    out << "build_tree\n";
    out << "set_thread_num 12\n";
    out << "set_accuracy 0.5\n";
    out << "set_max_iteration 1000\n";
    out << "set_print_interval 10\n";
    out << "set_use_isomorphism 1\n";
    out << "start_solve\n";
    out << "set_dump_rounds 2\n";
    out << "dump_result output_result.json\n";

    QApplication::clipboard()->setText(output_text);
    QMessageBox::information(this, "Success", "Parameters copied to clipboard!");
}

bool PT4ImportDialog::isPreflopImport() const
{
    return m_isPreflopImport;
}

void PT4ImportDialog::on_importPreflopButton_clicked()
{
    m_isPreflopImport = true;
    on_importButton_clicked();
}

void PT4ImportDialog::onHandSelectionChanged() {
    int row = ui->handsTable->currentRow();
    loadHandForVisualizer(row);
}

void PT4ImportDialog::onHandHovered(int row, int column) {
    Q_UNUSED(column);
    if (m_isSelectionLocked) return;

    if (row >= 0 && row != ui->handsTable->currentRow()) {
        ui->handsTable->selectRow(row);
    }
}

void PT4ImportDialog::onHandClicked(int row, int column) {
    Q_UNUSED(column);
    if (row >= 0) {
        if (m_isSelectionLocked && row == m_lockedRow) {
            // Clicked the locked row again -> Toggle off/Deselect!
            m_isSelectionLocked = false;
            m_lockedRow = -1;
            ui->handsTable->clearSelection();
            ui->handsTable->setCurrentItem(nullptr);
            m_currentHistoryText.clear();
            if (copyRawButton) copyRawButton->setEnabled(false);
            visualizer->clear();
        } else {
            // Lock to this row
            m_isSelectionLocked = true;
            m_lockedRow = row;
        }
    }
}

void PT4ImportDialog::loadHandForVisualizer(int row) {
    if (row < 0 || !db.isOpen()) {
        m_currentHistoryText.clear();
        if (copyRawButton) copyRawButton->setEnabled(false);
        visualizer->clear();
        return;
    }
    
    QTableWidgetItem* id_item = ui->handsTable->item(row, 0);
    if (!id_item) {
        m_currentHistoryText.clear();
        if (copyRawButton) copyRawButton->setEnabled(false);
        visualizer->clear();
        return;
    }
    
    int id_hand = id_item->data(Qt::UserRole).toInt();
    int cnt_players = ui->handsTable->item(row, 1)->data(Qt::UserRole).toInt();
    float amt_bb = ui->handsTable->item(row, 3)->data(Qt::UserRole).toFloat();
    if (amt_bb <= 0.0f) amt_bb = 2.0f; // Default fallback to 2
    
    ImportedHand hand;
    hand.cnt_players = cnt_players;
    hand.bb_size = amt_bb;
    hand.ante = 0.0f;
    hand.hand_no = ui->handsTable->item(row, 1)->text();
    hand.board = ui->handsTable->item(row, 2)->text();
    hand.pot = ui->handsTable->item(row, 3)->text().toFloat();
    hand.effective_stack = ui->handsTable->item(row, 4)->text().toFloat();
    hand.ip_player = ui->handsTable->item(row, 5)->text();
    hand.oop_player = ui->handsTable->item(row, 6)->text();
    hand.turn_card = ui->handsTable->item(row, 7) ? ui->handsTable->item(row, 7)->text() : "";
    hand.river_card = ui->handsTable->item(row, 8) ? ui->handsTable->item(row, 8)->text() : "";
    
    // Query all players dealt into the hand for preflop positions/stacks
    QSqlQuery qPlayers(db);
    qPlayers.prepare("SELECT p.player_name, ps.position, ps.amt_before, ps.amt_ante, "
                     "ps.flg_p_fold, ps.cnt_p_call, ps.cnt_p_raise "
                     "FROM tourney_hand_player_statistics ps "
                     "JOIN player p ON ps.id_player = p.id_player "
                     "WHERE ps.id_hand = :hid "
                     "ORDER BY ps.position ASC");
    qPlayers.bindValue(":hid", id_hand);
    
    if (qPlayers.exec()) {
        bool anteSet = false;
        QSet<QString> seenNames;
        while (qPlayers.next()) {
            PreflopPlayer player;
            player.name = qPlayers.value("player_name").toString().trimmed();
            if (seenNames.contains(player.name)) {
                continue;
            }
            seenNames.insert(player.name);
            player.position = qPlayers.value("position").toInt();
            float amt_before = qPlayers.value("amt_before").toFloat();
            player.stack = amt_before / amt_bb;
            player.folded = qPlayers.value("flg_p_fold").toBool();
            player.calls = qPlayers.value("cnt_p_call").toInt();
            player.raises = qPlayers.value("cnt_p_raise").toInt();
            
            if (!anteSet) {
                float raw_ante = qPlayers.value("amt_ante").toFloat();
                hand.ante = raw_ante / amt_bb;
                anteSet = true;
            }
            
            hand.preflop_players.push_back(player);
        }
    }
    
    // Query raw hand history text
    QString historyText;
    QSqlQuery qHistory(db);
    qHistory.prepare("SELECT history FROM tourney_hand_histories WHERE id_hand = :hid");
    qHistory.bindValue(":hid", id_hand);
    if (qHistory.exec() && qHistory.next()) {
        historyText = qHistory.value("history").toString();
    } else {
        qHistory.prepare("SELECT history FROM cash_hand_histories WHERE id_hand = :hid");
        qHistory.bindValue(":hid", id_hand);
        if (qHistory.exec() && qHistory.next()) {
            historyText = qHistory.value("history").toString();
        }
    }
    
    m_currentHistoryText = historyText;
    if (copyRawButton) {
        copyRawButton->setEnabled(!m_currentHistoryText.isEmpty());
    }
    
    visualizer->setHand(hand, historyText);
}

