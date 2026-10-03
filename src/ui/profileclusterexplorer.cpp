#include "include/ui/profileclusterexplorer.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QDebug>
#include <QHeaderView>
#include <QMouseEvent>
#include <QApplication>

ProfileClusterExplorer::ProfileClusterExplorer(QWidget *parent) :
    QDialog(parent),
    isUpdating(false)
{
    setWindowTitle("Profile Cluster Explorer");
    setWindowState(Qt::WindowMaximized);

    // Setup UI
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    
    QVBoxLayout *leftLayout = new QVBoxLayout();
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(10, 0, 0, 0);
    
    clusterTable = new QTableWidget(this);
    scenarioTable = new QTableWidget(this);
    positionTable = new QTableWidget(this);
    stackTable = new QTableWidget(this);
    
    clusterTable->setColumnCount(2);
    scenarioTable->setColumnCount(3);
    positionTable->setColumnCount(3);
    stackTable->setColumnCount(2);
    
    QTableWidget* tables[] = {clusterTable, scenarioTable, positionTable, stackTable};
    for (QTableWidget* table : tables) {
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setVisible(false);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectItems);
        table->setFocusPolicy(Qt::NoFocus);
        table->setStyleSheet(
            "QTableWidget { border: 1px solid #aaa; background-color: #eee; }"
            "QTableWidget::item { text-align: center; border: 1px solid #ddd; }"
            "QTableWidget::item:selected { background-color: #d12a3d; color: white; }"
            "QTableWidget::item:hover { background-color: #ddd; }"
        );
        table->viewport()->installEventFilter(this);
        connect(table, &QTableWidget::currentItemChanged, this, &ProfileClusterExplorer::onSelectionChanged);
    }
    
    rightLayout->addWidget(new QLabel("Cluster:"));
    rightLayout->addWidget(clusterTable, 2);
    rightLayout->addWidget(new QLabel("Scenario:"));
    rightLayout->addWidget(scenarioTable, 2);
    rightLayout->addWidget(new QLabel("Position:"));
    rightLayout->addWidget(positionTable, 3);
    rightLayout->addWidget(new QLabel("Stack:"));
    rightLayout->addWidget(stackTable, 3);
    
    // Range grid
    rangeView = new HtmlTableRangeView(this);
    QStringList ranks = {"A","K","Q","J","T","9","8","7","6","5","4","3","2"};
    rangeModel = new RangeSelectorTableModel(ranks, "", this, false);
    rangeDelegate = new RangeSelectorTableDelegate(ranks, rangeModel, this);
    rangeView->setModel(rangeModel);
    rangeView->setItemDelegate(rangeDelegate);
    rangeView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rangeView->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rangeView->horizontalHeader()->setVisible(true);
    rangeView->verticalHeader()->setVisible(true);
    
    leftLayout->addWidget(rangeView, 1);
    
    // Bottom layout for slider and save button
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    
    QLabel *sliderLabel = new QLabel("Range Weight:", this);
    weightSlider = new QSlider(Qt::Horizontal, this);
    weightSlider->setRange(0, 100);
    weightSlider->setValue(100);
    weightLabel = new QLabel("1.00", this);
    
    enablePaintBox = new QCheckBox("Enable Range Editing", this);
    enablePaintBox->setChecked(false);
    weightSlider->setEnabled(false);
    
    connect(enablePaintBox, &QCheckBox::toggled, this, [=](bool checked) {
        weightSlider->setEnabled(checked);
        saveButton->setEnabled(checked);
    });
    
    connect(weightSlider, &QSlider::valueChanged, this, [=](int value) {
        weightLabel->setText(QString::number(value / 100.0f, 'f', 2));
    });
    
    bottomLayout->addWidget(enablePaintBox);
    bottomLayout->addWidget(sliderLabel);
    bottomLayout->addWidget(weightSlider);
    bottomLayout->addWidget(weightLabel);
    
    bottomLayout->addSpacing(20);
    totalHandsLabel = new QLabel("<b>Total Hands:</b> 0", this);
    bottomLayout->addWidget(totalHandsLabel);
    
    bottomLayout->addSpacing(20);
    cleanRangeBox = new QCheckBox("Clean Range (Solver Mode)", this);
    cleanRangeBox->setChecked(true);
    bottomLayout->addWidget(cleanRangeBox);
    
    bottomLayout->addStretch();
    
    saveButton = new QPushButton("Save Range to Database", this);
    saveButton->setEnabled(false);
    bottomLayout->addWidget(saveButton);
    
    leftLayout->addLayout(bottomLayout);
    
    mainLayout->addLayout(leftLayout, 3);
    mainLayout->addLayout(rightLayout, 1);
    
    // Connect signals
    connect(saveButton, &QPushButton::clicked, this, &ProfileClusterExplorer::onSaveClicked);
    connect(cleanRangeBox, &QCheckBox::stateChanged, this, [this](int) { loadRange(); });
    
    // In order to catch edits on the grid
    connect(rangeView, &HtmlTableRangeView::item_release, this, &ProfileClusterExplorer::onRangeChanged);
    
    setupDatabase();
    loadDropdowns();
    loadRange();
}

ProfileClusterExplorer::~ProfileClusterExplorer()
{
}

void ProfileClusterExplorer::setupDatabase()
{
    if (QSqlDatabase::contains("ProfileExplorerDB")) {
        db = QSqlDatabase::database("ProfileExplorerDB");
    } else {
        db = QSqlDatabase::addDatabase("QPSQL", "ProfileExplorerDB");
        db.setHostName(qEnvironmentVariable("DB_SOLVER_HOST", "localhost"));
        db.setPort(qEnvironmentVariable("DB_SOLVER_PORT", "5432").toInt());
        db.setDatabaseName(qEnvironmentVariable("DB_SOLVER", "dbname"));
        db.setUserName(qEnvironmentVariable("DB_SOLVER_USER", "postgres"));
        db.setPassword(qEnvironmentVariable("DB_SOLVER_PASSWORD", "dbpass"));
    }

    if (!db.isOpen() && !db.open()) {
        QMessageBox::critical(this, "Database Error", "Failed to connect to database: " + db.lastError().text());
    }
}

bool ProfileClusterExplorer::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseMove) {
        if (QApplication::mouseButtons() & Qt::LeftButton) {
            QTableWidget *table = qobject_cast<QTableWidget*>(obj->parent());
            if (table) {
                QMouseEvent *me = static_cast<QMouseEvent*>(event);
                QTableWidgetItem *item = table->itemAt(me->pos());
                if (item && table->currentItem() != item) {
                    table->setCurrentItem(item);
                }
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

void ProfileClusterExplorer::loadDropdowns()
{
    isUpdating = true;
    
    QSqlQuery q(db);
    
    // Cluster
    q.exec("SELECT id_cluster, cluster_name, vpip_mean, pfr_mean FROM profile_clusters ORDER BY id_cluster");
    int row = 0, col = 0;
    clusterTable->setRowCount(0);
    
    clusterTable->insertRow(0);
    QTableWidgetItem *allClusterItem = new QTableWidgetItem("All Clusters");
    allClusterItem->setData(Qt::UserRole, 0);
    allClusterItem->setTextAlignment(Qt::AlignCenter);
    clusterTable->setItem(0, 0, allClusterItem);
    col = 1;
    
    while (q.next()) {
        if (col == 0) clusterTable->insertRow(clusterTable->rowCount());
        QString displayText = QString("%1\n(VPIP: %2%, PFR: %3%)")
                                  .arg(q.value(1).toString())
                                  .arg(q.value(2).toDouble(), 0, 'f', 1)
                                  .arg(q.value(3).toDouble(), 0, 'f', 1);
        QTableWidgetItem *item = new QTableWidgetItem(displayText);
        item->setData(Qt::UserRole, q.value(0).toInt());
        item->setTextAlignment(Qt::AlignCenter);
        clusterTable->setItem(clusterTable->rowCount() - 1, col, item);
        col = (col + 1) % 2;
    }
    
    // Scenario
    q.exec("SELECT id_scenario, name FROM action_scenarios ORDER BY id_scenario");
    row = 0; col = 0;
    scenarioTable->setRowCount(0);
    
    scenarioTable->insertRow(0);
    QTableWidgetItem *allScenarioItem = new QTableWidgetItem("All Scenarios");
    allScenarioItem->setData(Qt::UserRole, 0);
    allScenarioItem->setTextAlignment(Qt::AlignCenter);
    scenarioTable->setItem(0, 0, allScenarioItem);
    col = 1;
    
    while (q.next()) {
        if (col == 0) scenarioTable->insertRow(scenarioTable->rowCount());
        QTableWidgetItem *item = new QTableWidgetItem(q.value(1).toString());
        item->setData(Qt::UserRole, q.value(0).toInt());
        item->setTextAlignment(Qt::AlignCenter);
        scenarioTable->setItem(scenarioTable->rowCount() - 1, col, item);
        col = (col + 1) % 3;
    }
    
    // Position
    q.exec("SELECT id_position, name FROM positions ORDER BY seat_order");
    row = 0; col = 0;
    positionTable->setRowCount(0);
    
    positionTable->insertRow(0);
    QTableWidgetItem *allPositionItem = new QTableWidgetItem("All Positions");
    allPositionItem->setData(Qt::UserRole, 0);
    allPositionItem->setTextAlignment(Qt::AlignCenter);
    positionTable->setItem(0, 0, allPositionItem);
    col = 1;
    
    while (q.next()) {
        if (col == 0) positionTable->insertRow(positionTable->rowCount());
        QTableWidgetItem *item = new QTableWidgetItem(q.value(1).toString());
        item->setData(Qt::UserRole, q.value(0).toInt());
        item->setTextAlignment(Qt::AlignCenter);
        positionTable->setItem(positionTable->rowCount() - 1, col, item);
        col = (col + 1) % 3;
    }
    
    // Stack
    q.exec("SELECT id_stack_cluster, cluster_name, bb_min, bb_max FROM stack_clusters ORDER BY bb_min");
    row = 0; col = 0;
    stackTable->setRowCount(0);
    
    stackTable->insertRow(0);
    QTableWidgetItem *allItem = new QTableWidgetItem("All Stacks");
    allItem->setData(Qt::UserRole, 0);
    allItem->setTextAlignment(Qt::AlignCenter);
    stackTable->setItem(0, 0, allItem);
    col = 1;
    
    while (q.next()) {
        if (col == 0) stackTable->insertRow(stackTable->rowCount());
        QString displayText = QString("%1\n(%2bb - %3bb)")
                                  .arg(q.value(1).toString())
                                  .arg(q.value(2).toDouble(), 0, 'f', 0)
                                  .arg(q.value(3).toDouble(), 0, 'f', 0);
        QTableWidgetItem *item = new QTableWidgetItem(displayText);
        item->setData(Qt::UserRole, q.value(0).toInt());
        item->setTextAlignment(Qt::AlignCenter);
        stackTable->setItem(stackTable->rowCount() - 1, col, item);
        col = (col + 1) % 2;
    }
    
    if (!clusterTable->currentItem() && clusterTable->item(0, 0)) clusterTable->setCurrentItem(clusterTable->item(0, 0));
    if (!scenarioTable->currentItem() && scenarioTable->item(0, 0)) scenarioTable->setCurrentItem(scenarioTable->item(0, 0));
    if (!positionTable->currentItem() && positionTable->item(0, 0)) positionTable->setCurrentItem(positionTable->item(0, 0));
    if (!stackTable->currentItem() && stackTable->item(0, 0)) stackTable->setCurrentItem(stackTable->item(0, 0));
    
    isUpdating = false;
}

void ProfileClusterExplorer::onSelectionChanged()
{
    if (isUpdating) return;
    loadRange();
}

void ProfileClusterExplorer::onRangeChanged(int r, int c)
{
    if (!enablePaintBox->isChecked()) return;
    float newVal = weightSlider->value() / 100.0f;
    rangeModel->setRangeAt(r, c, newVal);
}

void ProfileClusterExplorer::loadRange()
{
    if (!db.isOpen()) return;
    if (!clusterTable->currentItem() || !scenarioTable->currentItem() || !positionTable->currentItem() || !stackTable->currentItem()) return;

    int c_id = clusterTable->currentItem()->data(Qt::UserRole).toInt();
    int s_id = scenarioTable->currentItem()->data(Qt::UserRole).toInt();
    int p_id = positionTable->currentItem()->data(Qt::UserRole).toInt();
    int st_id = stackTable->currentItem()->data(Qt::UserRole).toInt();
    
    bool useClean = cleanRangeBox->isChecked();
    QString queryStr = "SELECT range_text, range_true, range_counts FROM cluster_ranges WHERE 1=1";
    if (c_id != 0) queryStr += " AND id_cluster = :cid";
    if (s_id != 0) queryStr += " AND id_scenario = :sid";
    if (p_id != 0) queryStr += " AND id_position = :pid";
    if (st_id != 0) queryStr += " AND id_stack_cluster = :stid";
    
    QSqlQuery q(db);
    q.prepare(queryStr);
    
    if (c_id != 0) q.bindValue(":cid", c_id);
    if (s_id != 0) q.bindValue(":sid", s_id);
    if (p_id != 0) q.bindValue(":pid", p_id);
    if (st_id != 0) q.bindValue(":stid", st_id);
    
    if (c_id == 0 || s_id == 0 || p_id == 0 || st_id == 0) {
        if (q.exec()) {
            QMap<QString, float> sumProbMap;
            QMap<QString, float> sumCountMap;
            int numRows = 0;
            float maxCount = 0.0f;
            while(q.next()) {
                QString probTxt = useClean ? q.value(0).toString() : q.value(1).toString();
                QString countsTxt = q.value(2).toString();
                
                QStringList probParts = probTxt.split(",");
                for (const QString& part : probParts) {
                    QStringList kv = part.split(":");
                    if (kv.size() >= 2) sumProbMap[kv[0]] += kv[1].toFloat();
                }
                
                QStringList countParts = countsTxt.split(",");
                for (const QString& part : countParts) {
                    QStringList kv = part.split(":");
                    if (kv.size() >= 2) {
                        float c = kv[1].toFloat();
                        sumCountMap[kv[0]] += c;
                    }
                }
                numRows++;
            }
            if (numRows > 0) {
                // Find max count
                for (auto it = sumCountMap.begin(); it != sumCountMap.end(); ++it) {
                    if (it.value() > maxCount) maxCount = it.value();
                }
                
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
                
                QStringList newParts;
                float totalCount = 0.0f;
                for (auto it = sumCountMap.begin(); it != sumCountMap.end(); ++it) {
                    float sum_c = it.value();
                    totalCount += sum_c;
                    
                    float final_prob;
                    if (useClean) {
                        if (sum_c / ceilingCount < 0.20f) continue;
                        final_prob = std::min(1.0f, sum_c / ceilingCount);
                    } else {
                        if (maxCount > 0.001f) {
                            final_prob = std::min(1.0f, sum_c / maxCount);
                        } else {
                            final_prob = 0.0f;
                        }
                    }
                    
                    newParts << QString("%1:%2:%3").arg(it.key()).arg(final_prob, 0, 'f', 2).arg(sum_c, 0, 'f', 1);
                }
                rangeModel->setRangeText(newParts.join(","));
                totalHandsLabel->setText(QString("<b>Total Hands:</b> %1").arg(QString::number(totalCount, 'f', 0)));
            } else {
                rangeModel->clear_range();
                totalHandsLabel->setText("<b>Total Hands:</b> 0");
            }
        } else {
            rangeModel->clear_range();
            totalHandsLabel->setText("<b>Total Hands:</b> 0");
        }
    } else {
        if (q.exec() && q.next()) {
            QString probTxt = useClean ? q.value(0).toString() : q.value(1).toString();
            QString countsTxt = q.value(2).toString();
            
            QMap<QString, float> countMap;
            float totalCount = 0.0f;
            QStringList countParts = countsTxt.split(",");
            for (const QString& part : countParts) {
                QStringList kv = part.split(":");
                if (kv.size() >= 2) {
                    float c = kv[1].toFloat();
                    countMap[kv[0]] = c;
                    totalCount += c;
                }
            }
            
            QStringList newParts;
            QStringList probParts = probTxt.split(",");
            for (const QString& part : probParts) {
                QStringList kv = part.split(":");
                if (kv.size() >= 2) {
                    float c = countMap.value(kv[0], 0.0f);
                    newParts << QString("%1:%2:%3").arg(kv[0]).arg(kv[1]).arg(c, 0, 'f', 1);
                }
            }
            
            rangeModel->setRangeText(newParts.join(","));
            totalHandsLabel->setText(QString("<b>Total Hands:</b> %1").arg(QString::number(totalCount, 'f', 0)));
        } else {
            rangeModel->clear_range();
            totalHandsLabel->setText("<b>Total Hands:</b> 0");
        }
    }
}

void ProfileClusterExplorer::onSaveClicked()
{
    if (!db.isOpen()) return;
    if (!clusterTable->currentItem() || !scenarioTable->currentItem() || !positionTable->currentItem() || !stackTable->currentItem()) return;

    int c_id = clusterTable->currentItem()->data(Qt::UserRole).toInt();
    int s_id = scenarioTable->currentItem()->data(Qt::UserRole).toInt();
    int p_id = positionTable->currentItem()->data(Qt::UserRole).toInt();
    int st_id = stackTable->currentItem()->data(Qt::UserRole).toInt();
    
    if (c_id == 0 || s_id == 0 || p_id == 0 || st_id == 0) {
        QMessageBox::warning(this, "Save Failed", "You cannot save to an aggregate range. Please select specific properties to save your edits.");
        return;
    }
    
    QString newRange = rangeModel->getRangeText();
    
    QSqlQuery q(db);
    q.prepare("INSERT INTO cluster_ranges (id_cluster, id_position, id_scenario, id_stack_cluster, range_text) "
              "VALUES (:cid, :pid, :sid, :stid, :rng) "
              "ON CONFLICT (id_cluster, id_position, id_scenario, id_stack_cluster) "
              "DO UPDATE SET range_text = EXCLUDED.range_text");
    q.bindValue(":cid", c_id);
    q.bindValue(":pid", p_id);
    q.bindValue(":sid", s_id);
    q.bindValue(":stid", st_id);
    q.bindValue(":rng", newRange);
    
    if (q.exec()) {
        QMessageBox::information(this, "Success", "Range saved to database!");
    } else {
        QMessageBox::critical(this, "Error", "Failed to save range: " + q.lastError().text());
    }
}
