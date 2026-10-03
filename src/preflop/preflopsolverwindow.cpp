#include "include/preflop/preflopsolverwindow.h"
#include "ui_preflopsolverwindow.h"
#include <QFileDialog>
#ifdef USE_LIBTORCH
#include "include/preflop/NeuralNetEvaluator.h"
#endif
#include "include/tools/PrivateRangeConverter.h"
#include "include/preflop/PreflopTrainable.h"
#include "rangeselector.h"

#include <QHeaderView>
#include <QPainter>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <fstream>
#include <QRegularExpression>
#include <QFile>
#include "include/json.hpp"
static void clearLayout(QLayout* layout) {
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        } else if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout);
        }
        delete item;
    }
}

PreflopSolverWindow::PreflopSolverWindow(QSolverJob* solverJob, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::PreflopSolverWindow), qSolverJob(solverJob),
      solverThread(nullptr), progressTimer(nullptr), current_num_players(0)
{
    ui->setupUi(this);

    // Style elements for visual polish
    ui->playersTableWidget->setColumnCount(3);
    ui->playersTableWidget->setHorizontalHeaderLabels({"Position", "Stack (Chips)", "Range"});
    ui->playersTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->playersTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->playersTableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    // Initialize players table
    updatePlayersTable();

    // Models & View setup
    treeModel = new QStandardItemModel(this);
    ui->preflopTreeView->setModel(treeModel);

    strategyModel = new PreflopTableStrategyModel(this);
    strategyDelegate = new PreflopStrategyItemDelegate(this);
    ui->strategyTableView->setModel(strategyModel);
    ui->strategyTableView->setItemDelegate(strategyDelegate);

    // Stretch strategy grid items evenly
    ui->strategyTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->strategyTableView->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Connect signals and slots
    connect(ui->tableSizeSpinBox, SIGNAL(valueChanged(int)), this, SLOT(onTableSizeChanged(int)));
    connect(ui->playersTableWidget, SIGNAL(cellDoubleClicked(int, int)), this, SLOT(onPlayerTableCellDoubleClicked(int, int)));
    connect(ui->buildTreeButton, SIGNAL(clicked()), this, SLOT(onBuildTreeButtonClicked()));
    connect(ui->solveButton, SIGNAL(clicked()), this, SLOT(onSolveButtonClicked()));
    connect(ui->stopButton, SIGNAL(clicked()), this, SLOT(onStopButtonClicked()));
    connect(ui->saveButton, SIGNAL(clicked()), this, SLOT(onSaveButtonClicked()));
    connect(ui->loadButton, SIGNAL(clicked()), this, SLOT(onLoadButtonClicked()));
    connect(ui->generatePayoutsButton, SIGNAL(clicked()), this, SLOT(onGeneratePayoutsButtonClicked()));

    connect(ui->preflopTreeView->selectionModel(), SIGNAL(currentChanged(QModelIndex, QModelIndex)),
            this, SLOT(onTreeViewCurrentChanged(QModelIndex, QModelIndex)));

    // Timers
    progressTimer = new QTimer(this);
    connect(progressTimer, SIGNAL(timeout()), this, SLOT(onProgressTimerTimeout()));

    // Solve and Stop buttons initial states
    ui->solveButton->setEnabled(false);
    ui->stopButton->setEnabled(false);

    // Initialize default directory next to application binary
    QString default_dir = QDir(QCoreApplication::applicationDirPath()).filePath("solves/preflop");
    QDir().mkpath(default_dir);

    // Set up file system model for saved preflop solves
    solvesModel = new QFileSystemModel(this);
    solvesModel->setRootPath(default_dir);
    solvesModel->setNameFilters(QStringList() << "*.json" << "*.json.gz" << "*.json.z");
    solvesModel->setFilter(QDir::AllDirs | QDir::AllEntries | QDir::NoDotAndDotDot);

    // Set up proxy model to allow filtering/searching by name
    solvesProxyModel = new QSortFilterProxyModel(this);
    solvesProxyModel->setSourceModel(solvesModel);
    solvesProxyModel->setFilterKeyColumn(0); // Name column
    solvesProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->solvesTreeView->setModel(solvesProxyModel);
    ui->solvesTreeView->setRootIndex(solvesProxyModel->mapFromSource(solvesModel->index(default_dir)));

    // Ensure we set the root index correctly once the directory finishes loading asynchronously
    connect(solvesModel, &QFileSystemModel::directoryLoaded, this, [this, default_dir](const QString& path) {
        if (QDir(path) == QDir(default_dir)) {
            QModelIndex sourceIndex = solvesModel->index(default_dir);
            if (sourceIndex.isValid()) {
                ui->solvesTreeView->setRootIndex(solvesProxyModel->mapFromSource(sourceIndex));
            }
        }
    });

    for (int i = 1; i <= 4; ++i) {
        ui->solvesTreeView->hideColumn(i);
    }

    // Connect double-click on solvesTreeView to load it
    connect(ui->solvesTreeView, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(onSolveFileClicked(const QModelIndex&)));

    // Connect search filter box
    connect(ui->solvesFilterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        solvesProxyModel->setFilterFixedString(text);
    });

    // -------------------------------------------------------------
    // Payout Percentage & Auto-places calculation setup
    // -------------------------------------------------------------
    payoutTypeComboBox = new QComboBox(this);
    payoutTypeComboBox->addItem("Multi-Table Tournament (MTT)");
    payoutTypeComboBox->addItem("Sit & Go (SNG)");

    payoutPercentageComboBox = new QComboBox(this);
    payoutPercentageComboBox->addItem("13.6% (Standard MTT)");
    payoutPercentageComboBox->addItem("13.0% (Big / Bounty)");
    payoutPercentageComboBox->addItem("15.0% (Legacy MTT)");
    payoutPercentageComboBox->addItem("10.0% (Steep MTT)");
    payoutPercentageComboBox->addItem("18.0% (Hyper MTT)");
    payoutPercentageComboBox->addItem("Custom / SNG");

    entrantsSpinBox = new QSpinBox(this);
    entrantsSpinBox->setRange(1, 1000000);
    entrantsSpinBox->setValue(100);

    ui->payoutHelperFormLayout->insertRow(0, "Payout Curve:", payoutTypeComboBox);
    ui->payoutHelperFormLayout->insertRow(1, "Percent Paid:", payoutPercentageComboBox);
    ui->payoutHelperFormLayout->insertRow(2, "Entrants:", entrantsSpinBox);

    // Initial switch to MTT standard
    payoutTypeComboBox->setCurrentIndex(0);
    payoutPercentageComboBox->setCurrentIndex(0);
    entrantsSpinBox->setValue(100);
    ui->placesPaidSpinBox->setValue(14); // 100 * 0.136

    // Setup interactive connections for payout options
    connect(payoutPercentageComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        int idx = payoutPercentageComboBox->currentIndex();
        if (idx == 5) {
            entrantsSpinBox->setEnabled(false);
            payoutTypeComboBox->setCurrentIndex(1); // switch to SNG curve
        } else {
            entrantsSpinBox->setEnabled(true);
            payoutTypeComboBox->setCurrentIndex(0); // switch to MTT curve
            
            double pct = 0.136;
            if (idx == 0) pct = 0.136;
            else if (idx == 1) pct = 0.130;
            else if (idx == 2) pct = 0.150;
            else if (idx == 3) pct = 0.100;
            else if (idx == 4) pct = 0.180;
            
            int entrants = entrantsSpinBox->value();
            int places = qMax(1, (int)std::round(entrants * pct));
            ui->placesPaidSpinBox->setValue(places);
        }
    });

    connect(entrantsSpinBox, &QSpinBox::valueChanged, this, [this](int val) {
        int idx = payoutPercentageComboBox->currentIndex();
        if (idx != 5) {
            double pct = 0.136;
            if (idx == 0) pct = 0.136;
            else if (idx == 1) pct = 0.130;
            else if (idx == 2) pct = 0.150;
            else if (idx == 3) pct = 0.100;
            else if (idx == 4) pct = 0.180;
            
            int places = qMax(1, (int)std::round(val * pct));
            ui->placesPaidSpinBox->setValue(places);
        }
    });

    connect(payoutTypeComboBox, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (idx == 0) {
            if (payoutPercentageComboBox->currentIndex() == 5) {
                payoutPercentageComboBox->setCurrentIndex(0);
            }
        } else {
            payoutPercentageComboBox->setCurrentIndex(5);
        }
    });

    // Create horizontal layout for starting range viewer buttons
    rangeViewButtonsLayout = new QHBoxLayout();
    rangeViewButtonsLayout->setSpacing(5);
    ui->rightLayout->insertLayout(1, rangeViewButtonsLayout); // Insert right after rightHeaderLayout (index 0)

    // Create layout for tree expand/collapse navigation buttons (2x2 grid)
    QVBoxLayout* treeControlsLayout = new QVBoxLayout();
    treeControlsLayout->setSpacing(5);
    
    QHBoxLayout* treeRow1 = new QHBoxLayout();
    treeRow1->setSpacing(5);
    QPushButton* btnExpandAll = new QPushButton("Expand All", this);
    QPushButton* btnCollapseAll = new QPushButton("Collapse All", this);
    treeRow1->addWidget(btnExpandAll);
    treeRow1->addWidget(btnCollapseAll);
    
    QHBoxLayout* treeRow2 = new QHBoxLayout();
    treeRow2->setSpacing(5);
    QPushButton* btnExpandSelected = new QPushButton("Expand Selected", this);
    QPushButton* btnCollapseSelected = new QPushButton("Collapse Selected", this);
    treeRow2->addWidget(btnExpandSelected);
    treeRow2->addWidget(btnCollapseSelected);
    
    QString btnStyle = 
        "QPushButton { padding: 4px 6px; font-size: 11px; background-color: #333; color: #ddd; border: 1px solid #555; border-radius: 3px; }"
        "QPushButton:hover { background-color: #444; }"
        "QPushButton:pressed { background-color: #222; }";
    
    btnExpandAll->setStyleSheet(btnStyle);
    btnCollapseAll->setStyleSheet(btnStyle);
    btnExpandSelected->setStyleSheet(btnStyle);
    btnCollapseSelected->setStyleSheet(btnStyle);
    
    treeControlsLayout->addLayout(treeRow1);
    treeControlsLayout->addLayout(treeRow2);
    
    ui->middleLayout->addLayout(treeControlsLayout);

    QFrame* treeControlsSeparator = new QFrame(this);
    treeControlsSeparator->setFrameShape(QFrame::HLine);
    treeControlsSeparator->setFrameShadow(QFrame::Sunken);
    treeControlsSeparator->setStyleSheet("QFrame { background-color: #444; max-height: 1px; margin-top: 6px; margin-bottom: 6px; border: none; }");
    ui->middleLayout->addWidget(treeControlsSeparator);

    // -------------------------------------------------------------
    // Node Locking GroupBox Construction
    // -------------------------------------------------------------
    nodeLockGroupBox = new QGroupBox("Interactive Node Lock Editor", this);
    nodeLockGroupBox->setEnabled(false); // starts disabled until a node is selected
    
    QVBoxLayout* nodeLockLayout = new QVBoxLayout(nodeLockGroupBox);
    nodeLockLayout->setSpacing(6);
    nodeLockLayout->setContentsMargins(8, 8, 8, 8);
    
    lockSelectionLabel = new QLabel("Selected: [None]", this);
    lockSelectionLabel->setWordWrap(true);
    nodeLockLayout->addWidget(lockSelectionLabel);
    
    lockSlidersLayout = new QHBoxLayout();
    lockSlidersLayout->setSpacing(10);
    nodeLockLayout->addLayout(lockSlidersLayout);
    
    QVBoxLayout* buttonsLayout = new QVBoxLayout();
    buttonsLayout->setSpacing(5);
    
    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(5);
    lockSelectionButton = new QPushButton("Lock Selection", this);
    unlockSelectionButton = new QPushButton("Unlock Selection", this);
    row1->addWidget(lockSelectionButton);
    row1->addWidget(unlockSelectionButton);
    
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(5);
    clearSelectionButton = new QPushButton("Clear Selection", this);
    unlockAllButton = new QPushButton("Unlock All", this);
    row2->addWidget(clearSelectionButton);
    row2->addWidget(unlockAllButton);
    
    QString lockBtnStyle = 
        "QPushButton { padding: 4px 6px; font-size: 11px; background-color: #444; color: #ddd; border: 1px solid #666; border-radius: 3px; }"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton:pressed { background-color: #333; }"
        "QPushButton:disabled { background-color: #222; color: #555; border: 1px solid #333; }";
    
    lockSelectionButton->setStyleSheet(lockBtnStyle);
    unlockSelectionButton->setStyleSheet(lockBtnStyle);
    clearSelectionButton->setStyleSheet(lockBtnStyle);
    unlockAllButton->setStyleSheet(lockBtnStyle);
    
    buttonsLayout->addLayout(row1);
    buttonsLayout->addLayout(row2);
    nodeLockLayout->addLayout(buttonsLayout);
    
    ui->middleLayout->addWidget(nodeLockGroupBox);
    
    connect(lockSelectionButton, SIGNAL(clicked()), this, SLOT(onLockSelectionClicked()));
    connect(unlockSelectionButton, SIGNAL(clicked()), this, SLOT(onUnlockSelectionClicked()));
    connect(unlockAllButton, SIGNAL(clicked()), this, SLOT(onUnlockAllClicked()));
    connect(clearSelectionButton, &QPushButton::clicked, this, [this]() {
        ui->strategyTableView->clearSelection();
    });

    // Connect strategy grid selection changed
    connect(ui->strategyTableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)),
            this, SLOT(onTableViewSelectionChanged(QItemSelection, QItemSelection)));

    connect(btnExpandAll, &QPushButton::clicked, this, [this]() {
        ui->preflopTreeView->expandAll();
    });
    connect(btnCollapseAll, &QPushButton::clicked, this, [this]() {
        ui->preflopTreeView->collapseAll();
    });
    connect(btnExpandSelected, &QPushButton::clicked, this, [this]() {
        QModelIndex current = ui->preflopTreeView->currentIndex();
        if (current.isValid()) {
            setIndexExpandedRecursive(current, true);
        }
    });
    connect(btnCollapseSelected, &QPushButton::clicked, this, [this]() {
        QModelIndex current = ui->preflopTreeView->currentIndex();
        if (current.isValid()) {
            setIndexExpandedRecursive(current, false);
        }
    });

#ifdef USE_LIBTORCH
    // Neural net controls
    connect(ui->nnModelBrowseButton, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Select Neural Net Model", ".", "TorchScript Models (*.pt)");
        if (!path.isEmpty()) {
            ui->nnModelPathLineEdit->setText(path);
        }
    });

    // Auto-populate default model path if file exists
    QString defaultModelPath = QDir(QCoreApplication::applicationDirPath()).filePath("value_network_traced.pt");
    if (QFile::exists(defaultModelPath)) {
        ui->nnModelPathLineEdit->setText(defaultModelPath);
        ui->useNeuralNetCheckBox->setChecked(true);
    }
#else
    // Hide NN controls if LibTorch not compiled in
    ui->useNeuralNetCheckBox->setVisible(false);
    ui->labelNNModel->setVisible(false);
    ui->nnModelPathLineEdit->setVisible(false);
    ui->nnModelBrowseButton->setVisible(false);
#endif
}

PreflopSolverWindow::~PreflopSolverWindow() {
    if (solverThread && solverThread->isRunning()) {
        if (cfrSolver) {
            cfrSolver->stop();
        }
        solverThread->wait();
    }
    delete ui;
}

void PreflopSolverWindow::importPreflopHand(const ImportedHand& hand) {
    if (!hand.hand_no.isEmpty()) {
        ui->handNameLineEdit->setText(hand.hand_no);
    } else {
        ui->handNameLineEdit->clear();
    }
    auto preflop_players = hand.preflop_players;
    
    // Sort players in preflop action order
    std::sort(preflop_players.begin(), preflop_players.end(), [](const PreflopPlayer& a, const PreflopPlayer& b) {
        auto action_order = [](int pos) {
            if (pos == 8) return 10; // BB is last to act preflop
            if (pos == 9) return 9;  // SB is second to last
            return 8 - pos;          // UTG(7)->1, ..., BTN(0)->8
        };
        return action_order(a.position) < action_order(b.position);
    });
    
    // Keep only active players (who voluntarily called/raised) and the blinds (SB/BB: 9/8)
    std::vector<PreflopPlayer> filtered_players;
    for (const auto& p : preflop_players) {
        if (p.position == 9 || p.position == 8 || p.calls > 0 || p.raises > 0) {
            filtered_players.push_back(p);
        }
    }
    
    // Ensure we have at least 3 players to keep a reasonable table structure
    if (filtered_players.size() < 3) {
        for (const auto& p : preflop_players) {
            bool already_in = false;
            for (const auto& fp : filtered_players) {
                if (fp.name == p.name) {
                    already_in = true;
                    break;
                }
            }
            if (!already_in) {
                filtered_players.push_back(p);
                if (filtered_players.size() >= 3) {
                    break;
                }
            }
        }
    }
    
    // Sort players in preflop action order
    std::sort(filtered_players.begin(), filtered_players.end(), [](const PreflopPlayer& a, const PreflopPlayer& b) {
        auto action_order = [](int pos) {
            if (pos == 8) return 10;
            if (pos == 9) return 9;
            return 8 - pos;
        };
        return action_order(a.position) < action_order(b.position);
    });
    
    preflop_players = filtered_players;
    
    int num_players = preflop_players.size();
    if (num_players < 3) num_players = 3;
    if (num_players > 9) num_players = 9;
    
    ui->tableSizeSpinBox->setValue(num_players);
    updatePlayersTable();
    
    // Blinds & Ante
    ui->sbSpinBox->setValue(0.5);
    ui->bbSpinBox->setValue(1.0);
    ui->anteSpinBox->setValue(hand.ante);
    
    // Sizing defaults for reasonable tree size
    ui->rfiSpinBox->setValue(2.0);       // 2.0x BB open
    ui->threeBetSpinBox->setValue(3.0);   // 3x the open
    ui->fourBetSpinBox->setValue(2.0);    // 2x the 3-bet
    
    // Determine average stack depth to decide on shoving
    float avg_stack = 0.0f;
    for (const auto& p : preflop_players) {
        avg_stack += p.stack;
    }
    avg_stack /= preflop_players.size();
    
    // Disable all-in shove option when average stacks are deep (>30bb)
    // to prevent tree explosion; enable for short-stacked spots
    ui->allowAllInCheckBox->setChecked(avg_stack <= 30.0f);
    
    // Populate stacks and ranges
    for (int i = 0; i < qMin(num_players, (int)preflop_players.size()); ++i) {
        QTableWidgetItem* stackItem = ui->playersTableWidget->item(i, 1);
        if (stackItem) {
            stackItem->setText(QString::number(preflop_players[i].stack, 'f', 1));
        }
        
        QTableWidgetItem* rangeItem = ui->playersTableWidget->item(i, 2);
        if (rangeItem) {
            rangeItem->setText(preflop_players[i].range);
        }
    }
}

vector<string> PreflopSolverWindow::get_positions(int num_players) {
    if (num_players == 3) return {"BTN", "SB", "BB"};
    if (num_players == 4) return {"CO", "BTN", "SB", "BB"};
    if (num_players == 5) return {"UTG", "CO", "BTN", "SB", "BB"};
    if (num_players == 6) return {"UTG", "MP", "CO", "BTN", "SB", "BB"};
    if (num_players == 7) return {"UTG", "UTG+1", "MP", "CO", "BTN", "SB", "BB"};
    if (num_players == 8) return {"UTG", "UTG+1", "MP", "HJ", "CO", "BTN", "SB", "BB"};
    if (num_players == 9) return {"UTG", "UTG+1", "MP", "LJ", "HJ", "CO", "BTN", "SB", "BB"};
    return {};
}

void PreflopSolverWindow::updatePlayersTable() {
    int num_players = ui->tableSizeSpinBox->value();
    int old_rows = ui->playersTableWidget->rowCount();
    
    ui->playersTableWidget->setRowCount(num_players);
    vector<string> positions = get_positions(num_players);
    
    for (int i = 0; i < num_players; ++i) {
        // Position Label (Col 0)
        QTableWidgetItem* posItem = ui->playersTableWidget->item(i, 0);
        if (!posItem) {
            posItem = new QTableWidgetItem();
            ui->playersTableWidget->setItem(i, 0, posItem);
        }
        posItem->setText(QString::fromStdString(positions[i]));
        posItem->setFlags(posItem->flags() & ~Qt::ItemIsEditable); // Read-only
        
        // Stack Item (Col 1)
        QTableWidgetItem* stackItem = ui->playersTableWidget->item(i, 1);
        if (!stackItem) {
            stackItem = new QTableWidgetItem("1000");
            ui->playersTableWidget->setItem(i, 1, stackItem);
        }
        
        // Range Item (Col 2)
        QTableWidgetItem* rangeItem = ui->playersTableWidget->item(i, 2);
        if (!rangeItem) {
            QString defaultRange = "22+,A2s+,K2s+,QTs+,JTs,T8s+,98s,87s,76s,65s,54s,43s,A2o+,KTo+,QTo+,J9o+,T8o+,98o,87o,76o,65o,54o,43o";
            rangeItem = new QTableWidgetItem(defaultRange);
            ui->playersTableWidget->setItem(i, 2, rangeItem);
        }
        rangeItem->setFlags(rangeItem->flags() & ~Qt::ItemIsEditable); // Double-click launches popup editor
    }
}

void PreflopSolverWindow::onTableSizeChanged(int value) {
    updatePlayersTable();
    onGeneratePayoutsButtonClicked();
}

void PreflopSolverWindow::onPlayerTableCellDoubleClicked(int row, int column) {
    if (column != 2) return; // Only column 2 (Range) launches the editor
    
    QTableWidgetItem* item = ui->playersTableWidget->item(row, column);
    if (!item) return;
    
    QString current_range = item->text();
    QTextEdit* temp_edit = new QTextEdit(this);
    temp_edit->setText(current_range);
    
    RangeSelector selector(temp_edit, this);
    if (selector.exec() == QDialog::Accepted || true) {
        item->setText(temp_edit->toPlainText());
    }
    delete temp_edit;
}

void PreflopSolverWindow::onBuildTreeButtonClicked() {
    int num_players = ui->tableSizeSpinBox->value();
    current_num_players = num_players;
    
    // Parse stacks
    vector<float> stacks(num_players);
    for (int i = 0; i < num_players; ++i) {
        QTableWidgetItem* item = ui->playersTableWidget->item(i, 1);
        bool ok;
        float val = item ? item->text().toFloat(&ok) : 1000.0f;
        if (!ok || val <= 0.0f) {
            val = 1000.0f;
            if (item) item->setText("1000");
        }
        stacks[i] = val;
    }
    
    float sb = ui->sbSpinBox->value();
    float bb = ui->bbSpinBox->value();
    float ante = ui->anteSpinBox->value();
    
    // Parse Sizing Rules (BB)
    vector<float> open_sizes = { (float)ui->rfiSpinBox->value() };
    vector<float> raise_sizes = { (float)ui->threeBetSpinBox->value(), (float)ui->fourBetSpinBox->value() };
    
    // Build tree
    bool allow_allin = ui->allowAllInCheckBox->isChecked();
    gameTree = make_shared<PreflopGameTree>(num_players, stacks, sb, bb, ante, open_sizes, raise_sizes, allow_allin);
    
    try {
        treeRoot = gameTree->build();
    } catch (const TreeTooLargeException& e) {
        QMessageBox::critical(this, "Tree Too Large", 
            QString("The game tree exceeded the maximum node limit.\n\n%1\n\n"
                    "Suggestions:\n"
                    "• Reduce the number of players (recommended: 2-6)\n"
                    "• Reduce raise sizing options\n"
                    "• Disable all-in for deep stacks")
            .arg(QString::fromStdString(e.what())));
        treeRoot = nullptr;
        log(QString("ERROR: Tree build aborted - %1").arg(QString::fromStdString(e.what())));
        return;
    }
    
    if (!treeRoot) {
        QMessageBox::critical(this, "Error", "Failed to build preflop game tree.");
        return;
    }
    
    int node_count = gameTree->get_node_count();
    
    // Reset view models
    nodeMap.clear();
    treeModel->clear();
    
    populateTreeModel(treeModel->invisibleRootItem(), treeRoot, num_players);
    ui->preflopTreeView->expandToDepth(1);
    
    // Compile range strings
    current_player_ranges.resize(num_players);
    for (int i = 0; i < num_players; ++i) {
        QTableWidgetItem* range_item = ui->playersTableWidget->item(i, 2);
        string range_str = range_item ? range_item->text().toStdString() : "";
        
        vector<PrivateCards> cards = PrivateRangeConverter::rangeStr2Cards(range_str, {});
        if (cards.empty()) {
            QMessageBox::warning(this, "Warning", QString("Player %1 range is empty. Defaulting to 100%.").arg(i));
            QString defaultRange = "22+,A2s+,K2s+,QTs+,JTs,T8s+,98s,87s,76s,65s,54s,43s,A2o+,KTo+,QTo+,J9o+,T8o+,98o,87o,76o,65o,54o,43o";
            cards = PrivateRangeConverter::rangeStr2Cards(defaultRange.toStdString(), {});
            if (range_item) range_item->setText(defaultRange);
        }
        current_player_ranges[i] = cards;
    }
    
    // Estimate memory usage for trainables: ~6 * actions * hands * 4 bytes per action node
    // Only pre-initialize for manageable tree sizes (< 500K nodes)
    static const int PRE_INIT_NODE_LIMIT = 500000;
    if (node_count < PRE_INIT_NODE_LIMIT) {
        initialize_trainables(treeRoot, current_player_ranges);
        log(QString("Pre-initialized trainables for %1 nodes.").arg(node_count));
    } else {
        log(QString("Tree has %1 nodes (> %2 limit). Trainables will be initialized lazily during solving.")
            .arg(node_count).arg(PRE_INIT_NODE_LIMIT));
    }
    
    // Estimate memory: ~80KB per action node with 1326 hands and 3 actions
    float est_memory_mb = node_count * 80.0f / 1024.0f / 1024.0f;
    ui->statusLabel->setText(QString("Status: Tree built (%1 nodes, ~%2 MB est.). Ready to solve.")
        .arg(node_count).arg(QString::number(est_memory_mb, 'f', 0)));
    log(QString("Tree built: %1 nodes, %2 players. Estimated memory: ~%3 MB")
        .arg(node_count).arg(num_players).arg(QString::number(est_memory_mb, 'f', 0)));
    ui->solveButton->setEnabled(true);

    updateRangeViewButtons();
}

void PreflopSolverWindow::populateTreeModel(QStandardItem* parentItem, const shared_ptr<PreflopGameTreeNode>& node, int num_players) {
    if (!node) return;
    
    const PreflopRule& rule = node->getRule();
    float pot = rule.get_pot();
    
    QString label;
    if (node->getParent()) {
        auto parent_action = dynamic_pointer_cast<PreflopActionNode>(node->getParent());
        if (parent_action) {
            int acting_player = parent_action->getPlayer();
            string pos = get_positions(num_players)[acting_player];
            float rem_stack = rule.get_remaining_stack(acting_player);
            label = QString("%1 (Stack: %2 BB): %3 | Pot: %4 BB")
                        .arg(QString::fromStdString(pos))
                        .arg(QString::number(rem_stack, 'f', 1))
                        .arg(QString::fromStdString(node->getPathAction()))
                        .arg(QString::number(pot, 'f', 1));
        } else {
            label = QString("%1 | Pot: %2 BB")
                        .arg(QString::fromStdString(node->getPathAction()))
                        .arg(QString::number(pot, 'f', 1));
        }
    } else {
        label = QString("Preflop Root | Pot: %1 BB").arg(QString::number(pot, 'f', 1));
    }
    
    if (node->getType() == PreflopGameTreeNode::NodeType::SHOWDOWN) {
        label += " [Showdown]";
    } else if (node->getType() == PreflopGameTreeNode::NodeType::TERMINAL) {
        auto term = dynamic_pointer_cast<PreflopTerminalNode>(node);
        int winner = term->getWinner();
        string pos = get_positions(num_players)[winner];
        label += QString(" [Winner: %1]").arg(QString::fromStdString(pos));
    }
    
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        auto trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
        if (trainable && trainable->isLocked()) {
            label += " [LOCKED]";
        }
    }
    
    QStandardItem* item = new QStandardItem(label);
    item->setData(QVariant::fromValue((void*)node.get()), Qt::UserRole);
    nodeMap[node.get()] = node;
    
    parentItem->appendRow(item);
    
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        const auto& children = action_node->getChildren();
        for (const auto& child : children) {
            populateTreeModel(item, child, num_players);
        }
    }
}

void PreflopSolverWindow::initialize_trainables(const shared_ptr<PreflopGameTreeNode>& node, const vector<vector<PrivateCards>>& player_ranges) {
    if (!node) return;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        int player = action_node->getPlayer();
        if (player >= 0 && player < (int)player_ranges.size() && !action_node->getTrainable()) {
            auto trainable = make_shared<PreflopTrainable>(const_cast<vector<PrivateCards>*>(&player_ranges[player]), action_node->getActions().size());
            action_node->setTrainable(trainable);
        }
        for (const auto& child : action_node->getChildren()) {
            initialize_trainables(child, player_ranges);
        }
    }
}

void PreflopSolverWindow::onSolveButtonClicked() {
    if (!treeRoot) {
        QMessageBox::critical(this, "Error", "Please build the tree first.");
        return;
    }
    
    if (!qSolverJob) {
        QMessageBox::critical(this, "Error", "Solver Job context not found.");
        return;
    }
    
    shared_ptr<Dic5Compairer> compairer = qSolverJob->ps_holdem.get_compairer();
    if (!compairer) {
        QMessageBox::critical(this, "Error", "Hold'em Evaluator is not yet loaded in QSolverJob.");
        return;
    }
    // Parse payouts if ICM checked
    if (ui->icmCheckBox->isChecked()) {
        vector<float> payouts;
        QStringList parts = ui->payoutsLineEdit->text().split(',', Qt::SkipEmptyParts);
        for (QString p : parts) {
            bool ok;
            float val = p.trimmed().toFloat(&ok);
            if (ok) payouts.push_back(val);
        }
        if (payouts.empty()) {
            onGeneratePayoutsButtonClicked();
            parts = ui->payoutsLineEdit->text().split(',', Qt::SkipEmptyParts);
            for (QString p : parts) {
                bool ok;
                float val = p.trimmed().toFloat(&ok);
                if (ok) payouts.push_back(val);
            }
        }
        if (payouts.empty()) {
            QMessageBox::warning(this, "Empty Payouts", "ICM Mode is enabled but payouts are empty. Please set Prize Pool & Places Paid and click 'Generate Payouts'.");
            return;
        }
        
        // Automatically normalize payouts to sum to 100.0
        float total_sum = 0.0f;
        for (float val : payouts) total_sum += val;
        if (total_sum > 0.0f) {
            for (float &val : payouts) {
                val = (val / total_sum) * 100.0f;
            }
        }
        icmCalc = PreflopIcmCalculator(payouts);
    } else {
        icmCalc = PreflopIcmCalculator(); // Chip EV
    }
    // Set up Equity Manager
    equityManager = make_shared<PreflopEquityManager>(compairer);
    int profile_type = ui->eqrProfileComboBox->currentIndex();
    equityManager->set_profile_type(profile_type);
    
    // Instantiate background solver
    int iterations = ui->iterationsSpinBox->value();
    int threads_to_use = qSolverJob ? qSolverJob->thread_number : 12;
    if (threads_to_use <= 0) threads_to_use = 12;

    cfrSolver = make_shared<PreflopCfrSolver>(
        treeRoot,
        current_player_ranges,
        icmCalc,
        equityManager,
        compairer,
        iterations,
        false, // debug
        5,     // print interval
        threads_to_use
    );
    
#ifdef USE_LIBTORCH
    if (ui->useNeuralNetCheckBox->isChecked()) {
        QString modelPath = ui->nnModelPathLineEdit->text();
        if (!modelPath.isEmpty() && QFile::exists(modelPath)) {
            auto nn_eval = std::make_shared<NeuralNetEvaluator>(modelPath.toStdString());
            cfrSolver->setNeuralNetEvaluator(nn_eval);
        } else if (!modelPath.isEmpty()) {
            QMessageBox::warning(this, "Neural Net", "Model file not found. Falling back to Monte Carlo.");
        }
    }
#endif
    
    solverThread = new PreflopSolverThread(cfrSolver, this);
    connect(solverThread, &QThread::finished, this, &PreflopSolverWindow::onSolvingFinished);
    connect(solverThread, &PreflopSolverThread::solverProgress, this, &PreflopSolverWindow::onSolverProgress);
    
    // Disable inputs
    ui->solveButton->setEnabled(false);
    ui->buildTreeButton->setEnabled(false);
    ui->tableSizeSpinBox->setEnabled(false);
    ui->playersTableWidget->setEnabled(false);
    ui->stopButton->setEnabled(true);
    ui->progressBar->setValue(0);
    
    solverThread->start();
    progressTimer->start(500);
    log(QString("Solving started: %1 iterations, %2 players...").arg(iterations).arg(current_num_players));
}

void PreflopSolverWindow::onStopButtonClicked() {
    if (cfrSolver) {
        cfrSolver->stop();
        ui->statusLabel->setText("Status: Stopping solver...");
        ui->stopButton->setEnabled(false);
    }
}

void PreflopSolverWindow::onProgressTimerTimeout() {
    if (cfrSolver) {
        int max_iter = ui->iterationsSpinBox->value();
        int cur_iter = cfrSolver->last_iteration;
        if (cur_iter < 0) cur_iter = 0;
        
        double percent = (double)cur_iter / max_iter * 100.0;
        if (percent > 100.0) percent = 100.0;
        ui->progressBar->setValue((int)percent);
        
        float exploitability = cfrSolver->last_exploitability;
        ui->statusLabel->setText(QString("Solving: Iteration %1/%2 | Exploitability: %3 mBB/hand")
                                 .arg(cur_iter)
                                 .arg(max_iter)
                                 .arg(exploitability >= 0.0f ? QString::number(exploitability, 'f', 2) : "N/A"));
        
        // Refresh the strategy grid in real-time
        if (strategyModel) {
            strategyModel->refresh();
        }
        ui->strategyTableView->viewport()->update();
    }
}

void PreflopSolverWindow::onSolvingFinished() {
    progressTimer->stop();
    ui->progressBar->setValue(100);
    
    if (cfrSolver) {
        float expl = cfrSolver->last_exploitability;
        int iters = cfrSolver->last_iteration;
        ui->statusLabel->setText(QString("Status: Finished at iteration %1. Exploitability: %2 mBB/hand")
                                 .arg(iters)
                                 .arg(expl >= 0.0f ? QString::number(expl, 'f', 2) : "N/A"));
        log(QString("Solving finished. Iterations: %1, Exploitability: %2 mBB/hand")
            .arg(iters)
            .arg(expl >= 0.0f ? QString::number(expl, 'f', 2) : "N/A"));
    } else {
        ui->statusLabel->setText("Status: Finished solving.");
        log("Solving finished.");
    }
    
    if (solverThread) {
        solverThread->deleteLater();
        solverThread = nullptr;
    }
    
    ui->solveButton->setEnabled(true);
    ui->buildTreeButton->setEnabled(true);
    ui->tableSizeSpinBox->setEnabled(true);
    ui->playersTableWidget->setEnabled(true);
    ui->stopButton->setEnabled(false);
    
    // Refresh strategy table view
    QModelIndex currentIndex = ui->preflopTreeView->currentIndex();
    if (currentIndex.isValid()) {
        onTreeViewCurrentChanged(currentIndex, QModelIndex());
    }
}

void PreflopSolverWindow::onTreeViewCurrentChanged(const QModelIndex& current, const QModelIndex& previous) {
    for (QStandardItem* hi : highlightedTreeItems) {
        hi->setBackground(QBrush(Qt::NoBrush));
    }
    highlightedTreeItems.clear();

    QStandardItem* item = treeModel->itemFromIndex(current);
    if (!item) return;

    QStandardItem* p = item;
    while (p) {
        p->setBackground(QBrush(QColor(60, 75, 95))); // Subtle dark blue-grey
        highlightedTreeItems.append(p);
        p = p->parent();
    }
    
    void* ptr = item->data(Qt::UserRole).value<void*>();
    if (!ptr) return;
    
    auto it = nodeMap.find(ptr);
    if (it != nodeMap.end()) {
        shared_ptr<PreflopGameTreeNode> node = it->second;
        strategyModel->setViewingStartingRangePlayer(-1);
        updateStartingRangeButtonsUI();
        strategyModel->setNodeAndRanges(node, current_player_ranges);
        
        if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
            auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
            int player = action_node->getPlayer();
            string pos = get_positions(current_num_players)[player];
            ui->activePlayerLabel->setText(QString("Active Position: %1").arg(QString::fromStdString(pos)));
            
            rebuildLockSliders(action_node->getActions().size(), action_node->getActions());
            updateLockEditorUI();
        } else {
            ui->activePlayerLabel->setText("Active Position: --");
            clearLayout(lockSlidersLayout);
            lockSliders.clear();
            lockSpinBoxes.clear();
            if (nodeLockGroupBox) {
                nodeLockGroupBox->setEnabled(false);
            }
            if (lockSelectionLabel) {
                lockSelectionLabel->setText("Selected: [None]");
            }
        }
    }
}

QVariant PreflopTableStrategyModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    
    int row = index.row();
    int col = index.column();

    if (role == PreflopTableStrategyModel::IsLockedRole) {
        if (viewing_starting_range_player >= 0) return false;
        if (!active_node) return false;
        if (active_node->getType() != PreflopGameTreeNode::NodeType::ACTION) return false;
        
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(active_node);
        if (!action_node) return false;
        
        int player = action_node->getPlayer();
        if (player < 0 || player >= (int)player_ranges.size()) return false;
        
        const vector<PrivateCards>& p_range = player_ranges[player];
        auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
        if (!preflop_trainable || !preflop_trainable->isLocked()) return false;
        
        const vector<bool>& locked_mask = preflop_trainable->getLockedMask();
        int hand_number = p_range.size();
        if (hand_number == 0 || locked_mask.size() != (size_t)hand_number) return false;
        
        for (int h = 0; h < hand_number; ++h) {
            if (!locked_mask[h]) continue;
            const PrivateCards& pc = p_range[h];
            int g1 = 12 - (pc.card1 / 4);
            int g2 = 12 - (pc.card2 / 4);
            int r, c;
            if (pc.card1 / 4 == pc.card2 / 4) {
                r = min(g1, g2);
                c = min(g1, g2);
            } else if (pc.card1 % 4 == pc.card2 % 4) {
                r = min(g1, g2);
                c = max(g1, g2);
            } else {
                r = max(g1, g2);
                c = min(g1, g2);
            }
            if (r == row && c == col) {
                return true;
            }
        }
        return false;
    }
    
    if (role == Qt::DisplayRole) {
        if (row == col) {
            return ranks[row] + ranks[row];
        } else if (row < col) {
            return ranks[row] + ranks[col] + "s";
        } else {
            return ranks[col] + ranks[row] + "o";
        }
    }
    
    if (role == PreflopTableStrategyModel::StrategyDataRole || role == Qt::ToolTipRole || role == PreflopTableStrategyModel::EvDataRole) {
        if (viewing_starting_range_player >= 0) {
            int player = viewing_starting_range_player;
            if (player < 0 || player >= (int)player_ranges.size()) return QVariant();
            const vector<PrivateCards>& p_range = player_ranges[player];
            
            int hand_number = p_range.size();
            if (hand_number == 0) return QVariant();
            
            double sum_weights = 0.0;
            bool has_reach = (player < (int)all_reach_probs.size() && all_reach_probs[player].size() == p_range.size());
            for (int h = 0; h < hand_number; ++h) {
                const PrivateCards& pc = p_range[h];
                int g1 = 12 - (pc.card1 / 4);
                int g2 = 12 - (pc.card2 / 4);
                int r, c;
                if (pc.card1 / 4 == pc.card2 / 4) {
                    r = min(g1, g2);
                    c = min(g1, g2);
                } else if (pc.card1 % 4 == pc.card2 % 4) {
                    r = min(g1, g2);
                    c = max(g1, g2);
                } else {
                    r = max(g1, g2);
                    c = min(g1, g2);
                }
                
                if (r == row && c == col) {
                    float reach = has_reach ? all_reach_probs[player][h] : 1.0f;
                    sum_weights += pc.weight * reach;
                }
            }
            
            double max_combos = (row == col) ? 6.0 : ((row < col) ? 4.0 : 12.0);
            double prob = sum_weights / max_combos;
            
            if (prob > 0.0) {
                if (role == PreflopTableStrategyModel::StrategyDataRole) {
                    QList<QVariant> list;
                    QMap<QString, QVariant> map;
                    map["action"] = "RANGE";
                    map["prob"] = prob;
                    list.append(map);
                    return list;
                } else if (role == Qt::ToolTipRole) {
                    // Tooltip
                    QString tooltip = QString("Hand: %1\nWeight: %2\nCombo Count: %3/%4\n")
                        .arg(data(index, Qt::DisplayRole).toString())
                        .arg(QString::number(sum_weights / max_combos * 100.0, 'f', 1) + "%")
                        .arg(QString::number(sum_weights, 'f', 2))
                        .arg(QString::number(max_combos, 'f', 0));
                    return tooltip;
                } else if (role == PreflopTableStrategyModel::EvDataRole) {
                    return QString::number(prob * 100.0, 'f', 1) + "%";
                } else {
                    return QVariant();
                }
            }
            return QVariant();
        }

        if (!active_node) return QVariant();
        if (active_node->getType() != PreflopGameTreeNode::NodeType::ACTION) return QVariant();
        
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(active_node);
        if (!action_node) return QVariant();
        
        int player = action_node->getPlayer();
        if (player < 0 || player >= (int)player_ranges.size()) return QVariant();
        
        const vector<PrivateCards>& p_range = player_ranges[player];
        shared_ptr<PreflopTrainable> trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
        if (!trainable) return QVariant();
        
        const vector<float>& avg_strategy = trainable->getAverageStrategy();
        const vector<string>& actions = action_node->getActions();
        const vector<float>& evs = trainable->getEvs();
        
        int hand_number = p_range.size();
        int action_number = actions.size();
        if (hand_number == 0 || action_number == 0) return QVariant();
        
        vector<double> strategy_sum(action_number, 0.0);
        double total_weight = 0.0;
        double total_ev = 0.0;
        
        for (int h = 0; h < hand_number; ++h) {
            const PrivateCards& pc = p_range[h];
            int g1 = 12 - (pc.card1 / 4);
            int g2 = 12 - (pc.card2 / 4);
            int r, c;
            if (pc.card1 / 4 == pc.card2 / 4) {
                r = min(g1, g2);
                c = min(g1, g2);
            } else if (pc.card1 % 4 == pc.card2 % 4) {
                r = min(g1, g2);
                c = max(g1, g2);
            } else {
                r = max(g1, g2);
                c = min(g1, g2);
            }
            
            if (r == row && c == col) {
                float reach = reach_probs.empty() ? 1.0f : reach_probs[h];
                total_weight += pc.weight * reach;
                
                float node_ev_h = 0.0f;
                for (int a = 0; a < action_number; ++a) {
                    strategy_sum[a] += pc.weight * reach * avg_strategy[a * hand_number + h];
                    if (!evs.empty()) {
                        node_ev_h += avg_strategy[a * hand_number + h] * evs[a * hand_number + h];
                    }
                }
                
                if (!evs.empty()) {
                    total_ev += pc.weight * reach * node_ev_h;
                }
            }
        }
        
        if (total_weight > 0.0) {
            double max_combos = (row == col) ? 6.0 : ((row < col) ? 4.0 : 12.0);
            double alpha = total_weight / max_combos;
            if (role == PreflopTableStrategyModel::StrategyDataRole) {
                QList<QVariant> list;
                for (int a = 0; a < action_number; ++a) {
                    QMap<QString, QVariant> map;
                    map["action"] = QString::fromStdString(actions[a]);
                    map["prob"] = strategy_sum[a] / total_weight;
                    map["alpha"] = alpha;
                    list.append(map);
                }
                return list;
            } else if (role == PreflopTableStrategyModel::EvDataRole) {
                if (evs.empty()) return QVariant();
                float initial_stack = active_node->getRule().player_stacks[player];
                return (total_ev / total_weight) - initial_stack;
            } else {
                // Tooltip
                QString tooltip = QString("Hand: %1\nReach: %2%\nWeight: %3\nEV: %4\nStrategy:\n")
                    .arg(data(index, Qt::DisplayRole).toString())
                    .arg(QString::number(alpha * 100.0, 'f', 1))
                    .arg(QString::number(total_weight, 'f', 2))
                    .arg(evs.empty() ? "N/A" : QString::number(total_ev / total_weight, 'f', 4));
                for (int a = 0; a < action_number; ++a) {
                    tooltip += QString("- %1: %2%\n")
                        .arg(QString::fromStdString(actions[a]))
                        .arg(strategy_sum[a] / total_weight * 100.0, 0, 'f', 1);
                }
                return tooltip;
            }
        }
    }
    
    return QVariant();
}

void PreflopStrategyItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    painter->save();
    
    QVariant strategyData = index.model()->data(index, PreflopTableStrategyModel::StrategyDataRole);
    QVariant textData = index.model()->data(index, Qt::DisplayRole);
    
    // Always draw a sleek dark grey background first to ensure visual consistency
    painter->fillRect(option.rect, QColor(45, 45, 45));
    
    if (strategyData.isValid()) {
        QList<QVariant> list = strategyData.toList();
        
        double x_offset = option.rect.x();
        double y = option.rect.y();
        double h = option.rect.height();
        double w = option.rect.width();
        
        for (const QVariant& item : list) {
            QMap<QString, QVariant> map = item.toMap();
            QString action = map["action"].toString();
            double prob = map["prob"].toDouble();
            
            if (prob <= 0.0) continue;
            
            double alpha = map.contains("alpha") ? map["alpha"].toDouble() : 1.0;
            // Boost visibility slightly for low weights so colors are still discernible
            double boosted_alpha = 0.15 + 0.85 * alpha;
            int alpha_int = std::max(0, std::min(255, static_cast<int>(boosted_alpha * 255)));

            if (action == "RANGE") {
                // Paint whole cell yellow with alpha channel representing strength
                QColor color(255, 200, 0, static_cast<int>(prob * 255));
                painter->fillRect(option.rect, color);
            } else {
                QColor color = getActionColor(action.toStdString());
                color.setAlpha(alpha_int);
                double item_w = prob * w;
                painter->fillRect(QRectF(x_offset, y, item_w, h), color);
                x_offset += item_w;
            }
        }
    }
    
    if (textData.isValid()) {
        painter->setPen(Qt::white);
        QFont font = painter->font();
        font.setBold(true);
        int fontSize = qMax(6, qMin(10, option.rect.height() / 3));
        font.setPointSize(fontSize);
        painter->setFont(font);
        
        QString combo_text = textData.toString();
        QVariant evData = index.model()->data(index, PreflopTableStrategyModel::EvDataRole);
        
        if (evData.isValid()) {
            QString ev_text;
            if (evData.type() == QVariant::Double || evData.type() == (QVariant::Type)QMetaType::Float) {
                ev_text = QString::number(evData.toDouble(), 'f', 3);
            } else {
                ev_text = evData.toString();
            }
            
            // Always draw if evData is valid
            if (true) {
                QRectF topHalf = option.rect;
                topHalf.setBottom(option.rect.center().y());
                QRectF bottomHalf = option.rect;
                bottomHalf.setTop(option.rect.center().y());
                
                painter->drawText(topHalf, Qt::AlignBottom | Qt::AlignHCenter, combo_text);
                
                QFont evFont = font;
                evFont.setPointSize(qMax(5, fontSize - 1));
                evFont.setBold(false);
                painter->setFont(evFont);
                painter->drawText(bottomHalf, Qt::AlignTop | Qt::AlignHCenter, ev_text);
            } else {
                painter->drawText(option.rect, Qt::AlignCenter, combo_text);
            }
        } else {
            painter->drawText(option.rect, Qt::AlignCenter, combo_text);
        }
    }
    
    bool isSelected = option.state & QStyle::State_Selected;
    bool isLocked = index.model()->data(index, PreflopTableStrategyModel::IsLockedRole).toBool();
    
    if (isSelected) {
        painter->setPen(QPen(Qt::white, 3));
        painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
    } else if (isLocked) {
        painter->setPen(QPen(QColor(255, 215, 0), 2));
        painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
    } else {
        painter->setPen(QColor(60, 60, 60));
        painter->drawRect(option.rect);
    }
    
    if (isLocked) {
        // Draw small padlock icon in top-right corner
        int rect_x = option.rect.right() - 10;
        int rect_y = option.rect.top() + 3;
        painter->setPen(QPen(QColor(255, 215, 0), 1));
        painter->setBrush(QColor(255, 215, 0));
        painter->drawRect(rect_x, rect_y + 3, 6, 4);
        painter->setBrush(Qt::NoBrush);
        painter->drawArc(rect_x + 1, rect_y + 1, 4, 4, 0, 180 * 16);
    }
    
    painter->restore();
}

QColor PreflopStrategyItemDelegate::getActionColor(const string& action) const {
    QString act = QString::fromStdString(action).toUpper();
    if (act.contains("FOLD")) {
        return QColor(0, 150, 255);
    } else if (act.contains("CALL") || act.contains("CHECK") || act.contains("RANGE")) {
        return QColor(0, 200, 50);
    } else if (act.contains("ALL-IN") || act.contains("ALLIN") || act.contains("SHOVE")) {
        return QColor(150, 0, 150); // Purple for All-In shoves
    } else if (act.contains("RAISE") || act.contains("BET")) {
        return QColor(220, 50, 50);
    }
    return QColor(128, 128, 128);
}

void PreflopSolverWindow::log(const QString& msg) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui->logTextEdit->append(QString("[%1] %2").arg(timestamp).arg(msg));
}

void PreflopSolverWindow::onSolverProgress(int iteration, float exploitability, float avg_duration_ms, float first_duration_ms, float last_duration_ms) {
    log(QString("Iteration %1 | Exploitability: %2 mBB/hand | Duration: Last=%3ms, Avg=%4ms, First=%5ms")
        .arg(iteration)
        .arg(exploitability >= 0.0f ? QString::number(exploitability, 'f', 2) : "N/A")
        .arg(QString::number(last_duration_ms, 'f', 1))
        .arg(QString::number(avg_duration_ms, 'f', 1))
        .arg(QString::number(first_duration_ms, 'f', 1)));
}

void PreflopSolverWindow::onSaveButtonClicked() {
    if (!treeRoot || !cfrSolver) {
        QMessageBox::critical(this, "Error", "No solved preflop tree available to save. Please solve or build the tree first.");
        return;
    }

    // Ensure default directory exists next to executable
    QString default_dir = QDir(QCoreApplication::applicationDirPath()).filePath("solves/preflop");
    QDir().mkpath(default_dir);

    // Automatically construct an informative default filename
    int num_players = ui->tableSizeSpinBox->value();
    QString filename_desc = QString("preflop_%1p").arg(num_players);
    for (int i = 0; i < num_players; ++i) {
        QTableWidgetItem* pos_item = ui->playersTableWidget->item(i, 0);
        QTableWidgetItem* stack_item = ui->playersTableWidget->item(i, 1);
        if (pos_item && stack_item) {
            QString pos = pos_item->text().trimmed();
            QString stack = stack_item->text().trimmed();
            pos.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "");
            stack.replace(QRegularExpression("[^0-9.]"), "");
            filename_desc += QString("_%1_%2").arg(pos).arg(stack);
        }
    }
    filename_desc += QString("_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QString hand_name = ui->handNameLineEdit->text().trimmed();
    if (!hand_name.isEmpty()) {
        hand_name.replace(QRegularExpression("[^a-zA-Z0-9_\\-\\s]"), "_");
        filename_desc = QString("%1_%2").arg(hand_name).arg(filename_desc);
    }
    filename_desc += ".json.gz";

    QString default_file = QDir(default_dir).filePath(filename_desc);

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Preflop Solve"),
                                                    default_file,
                                                    tr("Compressed Preflop Solves (*.json.gz *.json.z);;Uncompressed Preflop Solves (*.json)"));
    if (fileName.isEmpty()) return;

    // Build meta information
    json solve_json;
    solve_json["hand_name"] = ui->handNameLineEdit->text().toStdString();
    solve_json["players_count"] = ui->tableSizeSpinBox->value();
    solve_json["small_blind"] = ui->sbSpinBox->value();
    solve_json["big_blind"] = ui->bbSpinBox->value();
    solve_json["ante"] = ui->anteSpinBox->value();
    solve_json["rfi_size"] = ui->rfiSpinBox->value();
    solve_json["three_bet_size"] = ui->threeBetSpinBox->value();
    solve_json["four_bet_size"] = ui->fourBetSpinBox->value();
    solve_json["allow_allin"] = ui->allowAllInCheckBox->isChecked();
    solve_json["icm_enabled"] = ui->icmCheckBox->isChecked();
    solve_json["icm_payouts"] = ui->payoutsLineEdit->text().toStdString();
    solve_json["eqr_profile_index"] = ui->eqrProfileComboBox->currentIndex();
    solve_json["iterations"] = ui->iterationsSpinBox->value();

    json players_arr = json::array();
    for (int i = 0; i < num_players; ++i) {
        json p;
        QTableWidgetItem* pos_item = ui->playersTableWidget->item(i, 0);
        QTableWidgetItem* stack_item = ui->playersTableWidget->item(i, 1);
        QTableWidgetItem* range_item = ui->playersTableWidget->item(i, 2);
        p["position"] = pos_item ? pos_item->text().toStdString() : "";
        p["stack"] = stack_item ? stack_item->text().toDouble() : 1000.0;
        p["range"] = range_item ? range_item->text().toStdString() : "";
        players_arr.push_back(p);
    }
    solve_json["players"] = players_arr;

    // Serialize tree
    solve_json["solve_tree"] = cfrSolver->dumps_solve(treeRoot);

    // Save file
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Error", "Failed to open file for writing.");
        return;
    }

    if (fileName.endsWith(".json.gz", Qt::CaseInsensitive) || fileName.endsWith(".json.z", Qt::CaseInsensitive)) {
        std::string json_str = solve_json.dump(); // Raw dump without pretty-print whitespace
        QByteArray compressed = qCompress(QByteArray::fromStdString(json_str), 9);
        file.write(compressed);
    } else {
        std::string json_str = solve_json.dump(4); // Uncompressed, pretty-printed
        file.write(QByteArray::fromStdString(json_str));
    }
    file.close();

    // Force QFileSystemModel to refresh the directory so the saved file immediately appears
    solvesModel->setRootPath("");
    solvesModel->setRootPath(default_dir);
    QModelIndex sourceIdx = solvesModel->index(default_dir);
    if (sourceIdx.isValid()) {
        ui->solvesTreeView->setRootIndex(solvesProxyModel->mapFromSource(sourceIdx));
    }

    log(QString("Preflop solve saved successfully to: %1").arg(fileName));
    QMessageBox::information(this, "Success", "Solve saved successfully.");
}

void PreflopSolverWindow::onLoadButtonClicked() {
    // Ensure default directory next to executable
    QString default_dir = QDir(QCoreApplication::applicationDirPath()).filePath("solves/preflop");
    QDir().mkpath(default_dir);

    QString fileName = QFileDialog::getOpenFileName(this, tr("Load Preflop Solve"),
                                                    default_dir,
                                                    tr("Preflop Solve Files (*.json)"));
    loadSolveFromFile(fileName);
}

void PreflopSolverWindow::onSolveFileClicked(const QModelIndex& index) {
    QModelIndex sourceIndex = solvesProxyModel->mapToSource(index);
    if (!solvesModel->isDir(sourceIndex)) {
        QString filePath = solvesModel->filePath(sourceIndex);
        if (filePath.endsWith(".json", Qt::CaseInsensitive) ||
            filePath.endsWith(".json.gz", Qt::CaseInsensitive) ||
            filePath.endsWith(".json.z", Qt::CaseInsensitive)) {
            loadSolveFromFile(filePath);
        }
    }
}

void PreflopSolverWindow::loadSolveFromFile(const QString& fileName) {
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Failed to open solve file.");
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        QMessageBox::critical(this, "Error", "Solve file is empty.");
        return;
    }

    QByteArray uncompressed;
    QByteArray trimmed = data.trimmed();
    if (!trimmed.isEmpty() && (trimmed.startsWith("{") || trimmed.startsWith("["))) {
        uncompressed = data;
    } else {
        uncompressed = qUncompress(data);
        if (uncompressed.isEmpty()) {
            uncompressed = data;
        }
    }

    json solve_json;
    try {
        solve_json = json::parse(uncompressed.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", QString("Failed to parse JSON solve file: %1").arg(e.what()));
        return;
    }

    // Disable updates during GUI updates to prevent flicker
    ui->playersTableWidget->setUpdatesEnabled(false);

    // 1. Re-populate setup parameters in GUI
    if (solve_json.contains("players_count")) ui->tableSizeSpinBox->setValue(solve_json["players_count"].get<int>());
    if (solve_json.contains("small_blind")) ui->sbSpinBox->setValue(solve_json["small_blind"].get<double>());
    if (solve_json.contains("big_blind")) ui->bbSpinBox->setValue(solve_json["big_blind"].get<double>());
    if (solve_json.contains("ante")) ui->anteSpinBox->setValue(solve_json["ante"].get<double>());
    if (solve_json.contains("rfi_size")) ui->rfiSpinBox->setValue(solve_json["rfi_size"].get<double>());
    if (solve_json.contains("three_bet_size")) ui->threeBetSpinBox->setValue(solve_json["three_bet_size"].get<double>());
    if (solve_json.contains("four_bet_size")) ui->fourBetSpinBox->setValue(solve_json["four_bet_size"].get<double>());
    if (solve_json.contains("allow_allin")) ui->allowAllInCheckBox->setChecked(solve_json["allow_allin"].get<bool>());
    if (solve_json.contains("icm_enabled")) ui->icmCheckBox->setChecked(solve_json["icm_enabled"].get<bool>());
    if (solve_json.contains("icm_payouts")) ui->payoutsLineEdit->setText(QString::fromStdString(solve_json["icm_payouts"].get<string>()));
    if (solve_json.contains("eqr_profile_index")) ui->eqrProfileComboBox->setCurrentIndex(solve_json["eqr_profile_index"].get<int>());
    if (solve_json.contains("iterations")) ui->iterationsSpinBox->setValue(solve_json["iterations"].get<int>());

    // Populate Hand Name
    if (solve_json.contains("hand_name")) {
        ui->handNameLineEdit->setText(QString::fromStdString(solve_json["hand_name"].get<string>()));
    } else {
        // Try to extract from the file name if it has the prefix
        QFileInfo fi(fileName);
        QString base = fi.completeBaseName();
        if (base.contains("_preflop_")) {
            QString extracted = base.section("_preflop_", 0, 0);
            ui->handNameLineEdit->setText(extracted);
        } else {
            ui->handNameLineEdit->clear();
        }
    }

    // 2. Re-populate players table
    if (solve_json.contains("players")) {
        json players = solve_json["players"];
        int num_players = players.size();
        ui->tableSizeSpinBox->setValue(num_players);
        updatePlayersTable();
        for (int i = 0; i < num_players; ++i) {
            json p = players[i];
            if (p.contains("position") && ui->playersTableWidget->item(i, 0)) {
                ui->playersTableWidget->item(i, 0)->setText(QString::fromStdString(p["position"].get<string>()));
            }
            if (p.contains("stack") && ui->playersTableWidget->item(i, 1)) {
                ui->playersTableWidget->item(i, 1)->setText(QString::number(p["stack"].get<double>()));
            }
            if (p.contains("range") && ui->playersTableWidget->item(i, 2)) {
                ui->playersTableWidget->item(i, 2)->setText(QString::fromStdString(p["range"].get<string>()));
            }
        }
    }

    ui->playersTableWidget->setUpdatesEnabled(true);

    // 3. Rebuild the betting tree structure
    onBuildTreeButtonClicked();
    if (!treeRoot) {
        QMessageBox::critical(this, "Error", "Failed to rebuild betting tree structure for the loaded solve.");
        return;
    }

    // 4. Instantiate solver object
    shared_ptr<Dic5Compairer> compairer = qSolverJob->ps_holdem.get_compairer();
    if (!compairer) {
        QMessageBox::critical(this, "Error", "Hold'em Evaluator is not yet loaded in QSolverJob.");
        return;
    }

    if (ui->icmCheckBox->isChecked()) {
        vector<float> payouts;
        QStringList parts = ui->payoutsLineEdit->text().split(',', Qt::SkipEmptyParts);
        for (QString p : parts) {
            bool ok;
            float val = p.trimmed().toFloat(&ok);
            if (ok) payouts.push_back(val);
        }
        if (payouts.empty()) {
            onGeneratePayoutsButtonClicked();
            parts = ui->payoutsLineEdit->text().split(',', Qt::SkipEmptyParts);
            for (QString p : parts) {
                bool ok;
                float val = p.trimmed().toFloat(&ok);
                if (ok) payouts.push_back(val);
            }
        }
        if (payouts.empty()) {
            QMessageBox::warning(this, "Empty Payouts", "ICM Mode is enabled but payouts are empty. Please set Prize Pool & Places Paid and click 'Generate Payouts'.");
            return;
        }
        
        // Automatically normalize payouts to sum to 100.0
        float total_sum = 0.0f;
        for (float val : payouts) total_sum += val;
        if (total_sum > 0.0f) {
            for (float &val : payouts) {
                val = (val / total_sum) * 100.0f;
            }
        }
        icmCalc = PreflopIcmCalculator(payouts);
    } else {
        icmCalc = PreflopIcmCalculator();
    }
    equityManager = make_shared<PreflopEquityManager>(compairer);
    equityManager->set_profile_type(ui->eqrProfileComboBox->currentIndex());

    int threads_to_use = qSolverJob ? qSolverJob->thread_number : 12;
    if (threads_to_use <= 0) threads_to_use = 12;

    cfrSolver = make_shared<PreflopCfrSolver>(
        treeRoot,
        current_player_ranges,
        icmCalc,
        equityManager,
        compairer,
        ui->iterationsSpinBox->value(),
        false,
        5,
        threads_to_use
    );

    // 5. Load strategy/EV values into nodes recursively
    if (solve_json.contains("solve_tree")) {
        cfrSolver->loads_strategy_and_evs(treeRoot, solve_json["solve_tree"]);
    }

    // Set UI buttons states
    ui->solveButton->setEnabled(true);
    ui->stopButton->setEnabled(false);
    ui->progressBar->setValue(100);
    ui->statusLabel->setText("Status: Solve loaded successfully.");

    log(QString("Preflop solve loaded successfully from: %1").arg(fileName));
    QMessageBox::information(this, "Success", "Solve loaded successfully.");
}

void PreflopSolverWindow::updateRangeViewButtons() {
    if (rangeViewButtonsLayout) {
        QLayoutItem* item;
        while ((item = rangeViewButtonsLayout->takeAt(0)) != nullptr) {
            if (QWidget* widget = item->widget()) {
                delete widget;
            }
            delete item;
        }
    }
    rangeViewButtons.clear();
    gtoStrategyButton = nullptr;
    
    if (current_num_players <= 0) return;
    
    vector<string> positions = get_positions(current_num_players);
    
    QLabel* label = new QLabel("View Starting Range:", this);
    label->setStyleSheet("font-weight: bold; color: #aaa; margin-right: 5px;");
    rangeViewButtonsLayout->addWidget(label);
    
    for (int i = 0; i < current_num_players; ++i) {
        QString pos = QString::fromStdString(positions[i]);
        QPushButton* btn = new QPushButton(pos, this);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setStyleSheet(
            "QPushButton { padding: 4px 8px; font-weight: bold; background-color: #333; color: #ddd; border: 1px solid #555; border-radius: 4px; }"
            "QPushButton:hover { background-color: #444; }"
            "QPushButton:checked { background-color: #00c832; color: white; border: 1px solid #00ff40; }"
        );
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            strategyModel->setViewingStartingRangePlayer(i);
            ui->activePlayerLabel->setText(QString("Viewing Starting Range: %1").arg(QString::fromStdString(get_positions(current_num_players)[i])));
            strategyModel->refresh();
        });
        rangeViewButtonsLayout->addWidget(btn);
        rangeViewButtons.append(btn);
    }
    
    gtoStrategyButton = new QPushButton("GTO Strategy", this);
    gtoStrategyButton->setCheckable(true);
    gtoStrategyButton->setAutoExclusive(true);
    gtoStrategyButton->setChecked(true);
    gtoStrategyButton->setStyleSheet(
        "QPushButton { padding: 4px 8px; font-weight: bold; background-color: #333; color: #ddd; border: 1px solid #555; border-radius: 4px; }"
        "QPushButton:hover { background-color: #444; }"
        "QPushButton:checked { background-color: #dc3232; color: white; border: 1px solid #ff4040; }"
    );
    connect(gtoStrategyButton, &QPushButton::clicked, this, [this]() {
        strategyModel->setViewingStartingRangePlayer(-1);
        
        QModelIndex currentIndex = ui->preflopTreeView->currentIndex();
        if (currentIndex.isValid()) {
            QStandardItem* item = treeModel->itemFromIndex(currentIndex);
            if (item) {
                void* ptr = item->data(Qt::UserRole).value<void*>();
                auto it = nodeMap.find(ptr);
                if (it != nodeMap.end()) {
                    auto node = it->second;
                    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
                        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
                        int player = action_node->getPlayer();
                        string pos = get_positions(current_num_players)[player];
                        ui->activePlayerLabel->setText(QString("Active Position: %1").arg(QString::fromStdString(pos)));
                        strategyModel->refresh();
                        return;
                    }
                }
            }
        }
        ui->activePlayerLabel->setText("Active Position: --");
        strategyModel->refresh();
    });
    rangeViewButtonsLayout->addWidget(gtoStrategyButton);
    rangeViewButtonsLayout->addStretch();
}

void PreflopSolverWindow::updateStartingRangeButtonsUI() {
    if (gtoStrategyButton) {
        gtoStrategyButton->setChecked(true);
    }
}

void PreflopSolverWindow::setIndexExpandedRecursive(const QModelIndex& index, bool expanded) {
    if (!index.isValid()) return;
    ui->preflopTreeView->setExpanded(index, expanded);
    int rows = treeModel->rowCount(index);
    for (int i = 0; i < rows; ++i) {
        QModelIndex childIndex = treeModel->index(i, 0, index);
        setIndexExpandedRecursive(childIndex, expanded);
    }
}

void PreflopSolverWindow::onGeneratePayoutsButtonClicked() {
    double pool = ui->prizePoolSpinBox->value();
    int places = ui->placesPaidSpinBox->value();
    if (places <= 0 || pool <= 0.0) return;

    double r = 0.65;
    if (payoutTypeComboBox && payoutTypeComboBox->currentIndex() == 0) {
        // Multi-Table Tournament (MTT) Flatter Curve
        r = 0.762;
    } else {
        // Sit & Go (SNG) Steeper Curve
        if (places == 2) r = 0.65;
        else if (places == 3) r = 0.63;
        else if (places == 4) r = 0.65;
        else if (places == 5) r = 0.653;
        else r = 0.65;
    }

    double coeff_sum = 0.0;
    for (int i = 0; i < places; ++i) {
        coeff_sum += pow(r, i);
    }

    double first_prize = pool / coeff_sum;
    vector<double> prizes(places);
    double calculated_sum = 0.0;
    for (int i = 0; i < places; ++i) {
        prizes[i] = round(first_prize * pow(r, i) * 100.0) / 100.0;
        calculated_sum += prizes[i];
    }

    double diff = pool - calculated_sum;
    if (abs(diff) > 0.001) {
        prizes[0] = round((prizes[0] + diff) * 100.0) / 100.0;
    }

    int table_size = ui->tableSizeSpinBox->value();
    int output_count = qMin(places, table_size);

    QStringList prize_strs;
    for (int i = 0; i < output_count; ++i) {
        prize_strs << QString::number(prizes[i], 'f', 2);
    }
    ui->payoutsLineEdit->setText(prize_strs.join(","));
}



void PreflopSolverWindow::rebuildLockSliders(int action_count, const vector<string>& actions) {
    clearLayout(lockSlidersLayout);
    lockSliders.clear();
    lockSpinBoxes.clear();

    for (int i = 0; i < action_count; ++i) {
        QVBoxLayout* itemLayout = new QVBoxLayout();
        itemLayout->setSpacing(2);

        QLabel* label = new QLabel(QString::fromStdString(actions[i]), this);
        label->setAlignment(Qt::AlignCenter);
        
        QColor color = strategyDelegate ? strategyDelegate->getActionColor(actions[i]) : QColor(128, 128, 128);
        label->setStyleSheet(QString("QLabel { background-color: %1; color: white; font-weight: bold; border-radius: 2px; padding: 2px; font-size: 10px; }").arg(color.name()));

        QSlider* slider = new QSlider(Qt::Vertical, this);
        slider->setRange(0, 100);
        slider->setValue(0);
        slider->setFixedHeight(80);

        QDoubleSpinBox* spinbox = new QDoubleSpinBox(this);
        spinbox->setRange(0.0, 1.0);
        spinbox->setSingleStep(0.01);
        spinbox->setDecimals(2);
        spinbox->setValue(0.0);
        spinbox->setFixedWidth(50);
        spinbox->setStyleSheet("QDoubleSpinBox { font-size: 10px; }");

        itemLayout->addWidget(label);
        itemLayout->addWidget(slider, 0, Qt::AlignCenter);
        itemLayout->addWidget(spinbox, 0, Qt::AlignCenter);

        lockSlidersLayout->addLayout(itemLayout);

        lockSliders.push_back(slider);
        lockSpinBoxes.push_back(spinbox);

        connect(slider, &QSlider::valueChanged, this, [this, i](int val) {
            onLockSliderValueChanged(val);
        });
        connect(spinbox, &QDoubleSpinBox::valueChanged, this, [this, i](double val) {
            onLockSpinBoxValueChanged(val);
        });
    }
}

void PreflopSolverWindow::onLockSliderValueChanged(int value) {
    QSlider* senderSlider = qobject_cast<QSlider*>(sender());
    if (!senderSlider) return;
    
    int senderIndex = -1;
    for (int i = 0; i < lockSliders.size(); ++i) {
        if (lockSliders[i] == senderSlider) {
            senderIndex = i;
            break;
        }
    }
    if (senderIndex == -1) return;

    for (auto* s : lockSliders) s->blockSignals(true);
    for (auto* sb : lockSpinBoxes) sb->blockSignals(true);

    int total_remaining = 100 - value;
    int current_others_sum = 0;
    for (int i = 0; i < lockSliders.size(); ++i) {
        if (i != senderIndex) {
            current_others_sum += lockSliders[i]->value();
        }
    }

    if (current_others_sum > 0) {
        for (int i = 0; i < lockSliders.size(); ++i) {
            if (i != senderIndex) {
                double ratio = (double)lockSliders[i]->value() / current_others_sum;
                lockSliders[i]->setValue(qRound(ratio * total_remaining));
            }
        }
    } else {
        int others_count = lockSliders.size() - 1;
        if (others_count > 0) {
            int equal_val = total_remaining / others_count;
            int remainder = total_remaining % others_count;
            for (int i = 0; i < lockSliders.size(); ++i) {
                if (i != senderIndex) {
                    lockSliders[i]->setValue(equal_val + (remainder-- > 0 ? 1 : 0));
                }
            }
        }
    }

    for (int i = 0; i < lockSliders.size(); ++i) {
        lockSpinBoxes[i]->setValue(lockSliders[i]->value() / 100.0);
    }

    for (auto* s : lockSliders) s->blockSignals(false);
    for (auto* sb : lockSpinBoxes) sb->blockSignals(false);
}

void PreflopSolverWindow::onLockSpinBoxValueChanged(double value) {
    QDoubleSpinBox* senderSpinBox = qobject_cast<QDoubleSpinBox*>(sender());
    if (!senderSpinBox) return;

    int senderIndex = -1;
    for (int i = 0; i < lockSpinBoxes.size(); ++i) {
        if (lockSpinBoxes[i] == senderSpinBox) {
            senderIndex = i;
            break;
        }
    }
    if (senderIndex == -1) return;

    for (auto* s : lockSliders) s->blockSignals(true);
    for (auto* sb : lockSpinBoxes) sb->blockSignals(true);

    int val = qRound(value * 100.0);
    lockSliders[senderIndex]->setValue(val);

    int total_remaining = 100 - val;
    int current_others_sum = 0;
    for (int i = 0; i < lockSliders.size(); ++i) {
        if (i != senderIndex) {
            current_others_sum += lockSliders[i]->value();
        }
    }

    if (current_others_sum > 0) {
        for (int i = 0; i < lockSliders.size(); ++i) {
            if (i != senderIndex) {
                double ratio = (double)lockSliders[i]->value() / current_others_sum;
                lockSliders[i]->setValue(qRound(ratio * total_remaining));
            }
        }
    } else {
        int others_count = lockSliders.size() - 1;
        if (others_count > 0) {
            int equal_val = total_remaining / others_count;
            int remainder = total_remaining % others_count;
            for (int i = 0; i < lockSliders.size(); ++i) {
                if (i != senderIndex) {
                    lockSliders[i]->setValue(equal_val + (remainder-- > 0 ? 1 : 0));
                }
            }
        }
    }

    for (int i = 0; i < lockSliders.size(); ++i) {
        lockSpinBoxes[i]->setValue(lockSliders[i]->value() / 100.0);
    }

    for (auto* s : lockSliders) s->blockSignals(false);
    for (auto* sb : lockSpinBoxes) sb->blockSignals(false);
}

vector<int> PreflopSolverWindow::getSelectedHandIndices(int player, const QModelIndexList& selected) {
    vector<int> indices;
    if (player < 0 || player >= (int)current_player_ranges.size()) return indices;
    const auto& range = current_player_ranges[player];
    
    QStringList ranks = QStringList() << "A" << "K" << "Q" << "J" << "T" << "9" << "8" << "7" << "6" << "5" << "4" << "3" << "2";

    for (int idx = 0; idx < (int)range.size(); ++idx) {
        const PrivateCards& pc = range[idx];
        int g1 = 12 - (pc.card1 / 4);
        int g2 = 12 - (pc.card2 / 4);
        int r, c;
        if (pc.card1 / 4 == pc.card2 / 4) {
            r = min(g1, g2);
            c = min(g1, g2);
        } else if (pc.card1 % 4 == pc.card2 % 4) {
            r = min(g1, g2);
            c = max(g1, g2);
        } else {
            r = max(g1, g2);
            c = min(g1, g2);
        }
        
        for (const QModelIndex& s : selected) {
            if (s.row() == r && s.column() == c) {
                indices.push_back(idx);
                break;
            }
        }
    }
    return indices;
}

void PreflopSolverWindow::onTableViewSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    (void)selected;
    (void)deselected;
    updateLockEditorUI();
    ui->strategyTableView->viewport()->update();
}

void PreflopSolverWindow::updateLockEditorUI() {
    if (!nodeLockGroupBox) return;

    QModelIndex currentIndex = ui->preflopTreeView->currentIndex();
    if (!currentIndex.isValid()) {
        nodeLockGroupBox->setEnabled(false);
        lockSelectionLabel->setText("Selected: [None]");
        return;
    }

    QStandardItem* item = treeModel->itemFromIndex(currentIndex);
    if (!item) {
        nodeLockGroupBox->setEnabled(false);
        lockSelectionLabel->setText("Selected: [None]");
        return;
    }

    void* ptr = item->data(Qt::UserRole).value<void*>();
    if (!ptr) {
        nodeLockGroupBox->setEnabled(false);
        lockSelectionLabel->setText("Selected: [None]");
        return;
    }

    auto it = nodeMap.find(ptr);
    if (it == nodeMap.end()) {
        nodeLockGroupBox->setEnabled(false);
        lockSelectionLabel->setText("Selected: [None]");
        return;
    }

    shared_ptr<PreflopGameTreeNode> node = it->second;
    if (node->getType() != PreflopGameTreeNode::NodeType::ACTION) {
        nodeLockGroupBox->setEnabled(false);
        lockSelectionLabel->setText("Selected: [None]");
        return;
    }

    nodeLockGroupBox->setEnabled(true);

    auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
    int player = action_node->getPlayer();
    
    QModelIndexList selected = ui->strategyTableView->selectionModel()->selectedIndexes();
    vector<int> hand_indices = getSelectedHandIndices(player, selected);

    if (selected.empty() || hand_indices.empty()) {
        lockSelectionLabel->setText("Selected: [None] (Select hands in grid)");
        lockSelectionButton->setEnabled(false);
        unlockSelectionButton->setEnabled(false);
        unlockAllButton->setEnabled(action_node->getTrainable() && dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable())->isLocked());
        
        for (auto* s : lockSliders) s->setEnabled(false);
        for (auto* sb : lockSpinBoxes) sb->setEnabled(false);
        return;
    }

    lockSelectionButton->setEnabled(true);
    
    auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
    bool is_locked = preflop_trainable && preflop_trainable->isLocked();
    unlockSelectionButton->setEnabled(is_locked);
    
    if (is_locked) {
        const auto& mask = preflop_trainable->getLockedMask();
        bool any_selected_locked = false;
        for (int idx : hand_indices) {
            if (idx < (int)mask.size() && mask[idx]) {
                any_selected_locked = true;
                break;
            }
        }
        unlockSelectionButton->setEnabled(any_selected_locked);
    }
    unlockAllButton->setEnabled(is_locked);

    for (auto* s : lockSliders) s->setEnabled(true);
    for (auto* sb : lockSpinBoxes) sb->setEnabled(true);

    QStringList hand_names;
    QStringList ranks = QStringList() << "A" << "K" << "Q" << "J" << "T" << "9" << "8" << "7" << "6" << "5" << "4" << "3" << "2";
    for (const QModelIndex& idx : selected) {
        int r = idx.row();
        int c = idx.column();
        QString name;
        if (r == c) name = ranks[r] + ranks[r];
        else if (r < c) name = ranks[r] + ranks[c] + "s";
        else name = ranks[c] + ranks[r] + "o";
        hand_names.append(name);
    }
    
    QString hands_str = hand_names.join(", ");
    if (hands_str.length() > 50) {
        hands_str = QString("%1 hands").arg(hand_names.size());
    }
    lockSelectionLabel->setText(QString("Selected: %1").arg(hands_str));

    int action_num = action_node->getActions().size();
    vector<double> avg_strategy(action_num, 0.0);
    int count = 0;

    if (preflop_trainable) {
        const vector<float>& current_strat = preflop_trainable->getAverageStrategy();
        int hand_num = current_player_ranges[player].size();
        for (int hand_idx : hand_indices) {
            count++;
            for (int a = 0; a < action_num; ++a) {
                avg_strategy[a] += current_strat[a * hand_num + hand_idx];
            }
        }
    }

    if (count > 0) {
        for (int a = 0; a < action_num; ++a) {
            avg_strategy[a] /= count;
        }
    } else {
        for (int a = 0; a < action_num; ++a) {
            avg_strategy[a] = 1.0 / action_num;
        }
    }

    for (auto* s : lockSliders) s->blockSignals(true);
    for (auto* sb : lockSpinBoxes) sb->blockSignals(true);

    for (int a = 0; a < action_num; ++a) {
        int val = qRound(avg_strategy[a] * 100.0);
        if (a < lockSliders.size()) {
            lockSliders[a]->setValue(val);
            lockSpinBoxes[a]->setValue(val / 100.0);
        }
    }

    for (auto* s : lockSliders) s->blockSignals(false);
    for (auto* sb : lockSpinBoxes) sb->blockSignals(false);
}

void PreflopSolverWindow::onLockSelectionClicked() {
    QModelIndex currentIndex = ui->preflopTreeView->currentIndex();
    if (!currentIndex.isValid()) return;

    QStandardItem* item = treeModel->itemFromIndex(currentIndex);
    if (!item) return;

    void* ptr = item->data(Qt::UserRole).value<void*>();
    if (!ptr) return;

    auto it = nodeMap.find(ptr);
    if (it == nodeMap.end()) return;

    shared_ptr<PreflopGameTreeNode> node = it->second;
    if (node->getType() != PreflopGameTreeNode::NodeType::ACTION) return;

    auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
    int player = action_node->getPlayer();
    int action_num = action_node->getActions().size();
    int hand_num = current_player_ranges[player].size();

    auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
    if (!preflop_trainable) {
        preflop_trainable = make_shared<PreflopTrainable>(&current_player_ranges[player], action_num);
        action_node->setTrainable(preflop_trainable);
    }

    vector<float> locked_strat;
    vector<bool> locked_mask;
    if (preflop_trainable->isLocked()) {
        locked_strat = preflop_trainable->getLockedStrategy();
        locked_mask = preflop_trainable->getLockedMask();
    } else {
        locked_strat = preflop_trainable->getAverageStrategy();
        locked_mask = vector<bool>(hand_num, false);
    }

    QModelIndexList selected = ui->strategyTableView->selectionModel()->selectedIndexes();
    vector<int> hand_indices = getSelectedHandIndices(player, selected);
    if (hand_indices.empty()) return;

    vector<float> slider_vals(action_num);
    for (int a = 0; a < action_num; ++a) {
        slider_vals[a] = (float)lockSliders[a]->value() / 100.0f;
    }

    for (int hand_idx : hand_indices) {
        locked_mask[hand_idx] = true;
        for (int a = 0; a < action_num; ++a) {
            locked_strat[a * hand_num + hand_idx] = slider_vals[a];
        }
    }

    preflop_trainable->lockStrategy(locked_strat, locked_mask);

    QString label = item->text();
    if (!label.contains(" [LOCKED]")) {
        item->setText(label + " [LOCKED]");
    }

    log(QString("Locked %1 hands for node: %2").arg(hand_indices.size()).arg(QString::fromStdString(action_node->getPathAction())));
    updateLockEditorUI();
    strategyModel->refresh();
}

void PreflopSolverWindow::onUnlockSelectionClicked() {
    QModelIndex currentIndex = ui->preflopTreeView->currentIndex();
    if (!currentIndex.isValid()) return;

    QStandardItem* item = treeModel->itemFromIndex(currentIndex);
    if (!item) return;

    void* ptr = item->data(Qt::UserRole).value<void*>();
    if (!ptr) return;

    auto it = nodeMap.find(ptr);
    if (it == nodeMap.end()) return;

    shared_ptr<PreflopGameTreeNode> node = it->second;
    if (node->getType() != PreflopGameTreeNode::NodeType::ACTION) return;

    auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
    auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
    if (!preflop_trainable || !preflop_trainable->isLocked()) return;

    int player = action_node->getPlayer();
    int hand_num = current_player_ranges[player].size();

    vector<float> locked_strat = preflop_trainable->getLockedStrategy();
    vector<bool> locked_mask = preflop_trainable->getLockedMask();

    QModelIndexList selected = ui->strategyTableView->selectionModel()->selectedIndexes();
    vector<int> hand_indices = getSelectedHandIndices(player, selected);
    if (hand_indices.empty()) return;

    for (int hand_idx : hand_indices) {
        if (hand_idx < (int)locked_mask.size()) {
            locked_mask[hand_idx] = false;
        }
    }

    bool any_locked = false;
    for (bool locked : locked_mask) {
        if (locked) {
            any_locked = true;
            break;
        }
    }

    if (any_locked) {
        preflop_trainable->lockStrategy(locked_strat, locked_mask);
    } else {
        preflop_trainable->unlockStrategy();
        
        QString label = item->text();
        label.replace(" [LOCKED]", "");
        item->setText(label);
    }

    log(QString("Unlocked %1 hands for node: %2").arg(hand_indices.size()).arg(QString::fromStdString(action_node->getPathAction())));
    updateLockEditorUI();
    strategyModel->refresh();
}

void PreflopSolverWindow::onUnlockAllClicked() {
    QModelIndex currentIndex = ui->preflopTreeView->currentIndex();
    if (!currentIndex.isValid()) return;

    QStandardItem* item = treeModel->itemFromIndex(currentIndex);
    if (!item) return;

    void* ptr = item->data(Qt::UserRole).value<void*>();
    if (!ptr) return;

    auto it = nodeMap.find(ptr);
    if (it == nodeMap.end()) return;

    shared_ptr<PreflopGameTreeNode> node = it->second;
    if (node->getType() != PreflopGameTreeNode::NodeType::ACTION) return;

    auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
    auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
    if (!preflop_trainable) return;

    preflop_trainable->unlockStrategy();

    QString label = item->text();
    label.replace(" [LOCKED]", "");
    item->setText(label);

    log(QString("Unlocked all hands for node: %2").arg(QString::fromStdString(action_node->getPathAction())));
    updateLockEditorUI();
    strategyModel->refresh();
}
