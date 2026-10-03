#include "strategyexplorer.h"
#include "ui_strategyexplorer.h"
#include "gtotrainerwindow.h"
#include "qstandarditemmodel.h"
#include <QDateTime>
#include <QBrush>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QColor>
#include <fstream>
#include <cmath>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include "include/Card.h"
#include "include/trainable/Trainable.h"

StrategyExplorer::StrategyExplorer(QWidget *parent,QSolverJob * qSolverJob) :
    QWidget(parent),
    ui(new Ui::StrategyExplorer)
{
    this->qSolverJob = qSolverJob;
    this->detailWindowSetting = DetailWindowSetting();
    ui->setupUi(this);
    
    // Create and add solving status label to the board layout (far right)
    solvingStatusLabel = new QLabel(this);
    solvingStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (this->ui->horizontalLayout_6) {
        this->ui->horizontalLayout_6->addStretch();
        this->ui->horizontalLayout_6->addWidget(solvingStatusLabel);
    }
    
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    // Make node ID label selectable for copy-paste
    this->ui->nodeDisplayLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    /*
    QStandardItemModel* model = new QStandardItemModel();
    for (int row = 0; row < 4; ++row) {
         QStandardItem *item = new QStandardItem(QString("%1").arg(row) );
         model->appendRow( item );
    }
    this->ui->gameTreeView->setModel(model);
    */
    // Initial Game Tree preview panel
    this->ui->gameTreeView->setTreeData(qSolverJob);
    connect(
                this->ui->gameTreeView,
                SIGNAL(expanded(const QModelIndex&)),
                this,
                SLOT(item_expanded(const QModelIndex&))
                );
    connect(
                this->ui->gameTreeView,
                SIGNAL(clicked(const QModelIndex&)),
                this,
                SLOT(item_clicked(const QModelIndex&))
                );
    connect(
                this->ui->gameTreeView->selectionModel(),
                SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)),
                this,
                SLOT(item_clicked(const QModelIndex&))
                );

    // Initize strategy(rough) table
    this->tableStrategyModel = new TableStrategyModel(this->qSolverJob,this);
    this->ui->strategyTableView->setModel(this->tableStrategyModel);
    this->delegate_strategy = new StrategyItemDelegate(this->qSolverJob,&(this->detailWindowSetting),this);
    this->ui->strategyTableView->setItemDelegate(this->delegate_strategy);

    Deck* deck = this->qSolverJob->get_solver()->get_deck();
    int index = 0;
    QString board_qstring = QString::fromStdString(this->qSolverJob->board);
    for(Card one_card: deck->getCards()){
        if(board_qstring.contains(QString::fromStdString(one_card.toString())))continue;
        QString card_str_formatted = QString::fromStdString(one_card.toFormattedString());
        this->ui->turnCardBox->addItem(card_str_formatted);
        this->ui->riverCardBox->addItem(card_str_formatted);

        if(card_str_formatted.contains(QString::fromLocal8Bit("♦️")) ||
                card_str_formatted.contains(QString::fromLocal8Bit("♥️️"))){
            this->ui->turnCardBox->setItemData(0, QBrush(Qt::red),Qt::ForegroundRole);
            this->ui->riverCardBox->setItemData(0, QBrush(Qt::red),Qt::ForegroundRole);
        }else{
            this->ui->turnCardBox->setItemData(0, QBrush(Qt::black),Qt::ForegroundRole);
            this->ui->riverCardBox->setItemData(0, QBrush(Qt::black),Qt::ForegroundRole);
        }

        this->cards.push_back(one_card);
        index += 1;
    }
    if(this->qSolverJob->get_solver()->getGameTree()->getRoot()->getRound() == GameTreeNode::GameRound::FLOP){
        this->tableStrategyModel->setTrunCard(this->cards[0]);
        this->tableStrategyModel->setRiverCard(this->cards[1]);
        this->ui->riverCardBox->setCurrentIndex(1);
    }
    else if(this->qSolverJob->get_solver()->getGameTree()->getRoot()->getRound() == GameTreeNode::GameRound::TURN){
        this->tableStrategyModel->setRiverCard(this->cards[0]);
        this->ui->turnCardBox->clear();
    }
    else if(this->qSolverJob->get_solver()->getGameTree()->getRoot()->getRound() == GameTreeNode::GameRound::RIVER){
        this->ui->turnCardBox->clear();
        this->ui->riverCardBox->clear();
    }

    // Initize timer for strategy auto update
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update_second()));
    timer->start(1000);

    // On mouse event of strategy table
    connect(this->ui->strategyTableView,SIGNAL(itemMouseChange(int,int)),this,SLOT(onMouseMoveEvent(int,int)));

    // Initize Detail Viewer window
    this->detailViewerModel = new DetailViewerModel(this->tableStrategyModel,&(this->detailWindowSetting),this);
    this->ui->detailView->setModel(this->detailViewerModel);
    this->detailItemItemDelegate = new DetailItemDelegate(&(this->detailWindowSetting),this);
    this->ui->detailView->setItemDelegate(this->detailItemItemDelegate);
    this->ui->detailView->setAutoSizeRows(true);
    this->ui->detailView->setMinRowHeight(105);
    this->ui->detailView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->ui->nodeDisplayLabel->hide();

    // Set fixed height for Rough Strategy View so it's compact and doesn't waste space (62px fits action, percentage, and combo count)
    this->ui->roughStrategyView->setFixedHeight(62);

    // Prevent the lock editor group from stretching vertically — it should stay compact
    this->ui->nodeLockEditorGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Initize Rough Strategy Viewer
    this->roughStrategyViewerModel = new RoughStrategyViewerModel(this->tableStrategyModel,this);
    this->ui->roughStrategyView->setModel(this->roughStrategyViewerModel);
    this->roughStrategyItemDelegate = new RoughStrategyItemDelegate(&(this->detailWindowSetting),this);
    this->ui->roughStrategyView->setItemDelegate(this->roughStrategyItemDelegate);

    // Interactive Node Lock Editor connections
    connect(this->ui->strategyTableView, &QTableView::clicked, this, &StrategyExplorer::onStrategyGridClicked);
    connect(this->ui->detailView, &QTableView::clicked, this, &StrategyExplorer::onDetailViewClicked);

    // Expression category selector
    this->ui->expressionSelectBox->addItem("[None]");
    this->ui->expressionSelectBox->addItem("Top Pair");
    this->ui->expressionSelectBox->addItem("Second Pair");
    this->ui->expressionSelectBox->addItem("Third Pair");
    this->ui->expressionSelectBox->addItem("Overpair");
    this->ui->expressionSelectBox->addItem("PP < Top Card");
    this->ui->expressionSelectBox->addItem("PP < 2nd Card");
    this->ui->expressionSelectBox->addItem("Underpair");
    this->ui->expressionSelectBox->addItem("Two Pair");
    this->ui->expressionSelectBox->addItem("Quads");
    this->ui->expressionSelectBox->addItem("Full House");
    this->ui->expressionSelectBox->addItem("Flush");
    this->ui->expressionSelectBox->addItem("Straight");
    this->ui->expressionSelectBox->addItem("Trips/Set");
    this->ui->expressionSelectBox->addItem("Flush Draw");
    this->ui->expressionSelectBox->addItem("Combo Draw");
    this->ui->expressionSelectBox->addItem("Straight Draw (8+ outs)");
    this->ui->expressionSelectBox->addItem("Straight Draw (4 outs)");
    this->ui->expressionSelectBox->addItem("Ace High");
    this->ui->expressionSelectBox->addItem("King High");
    connect(this->ui->expressionSelectBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StrategyExplorer::on_expressionSelectBox_currentIndexChanged);

    if (this->ui->gameTreeView->model() && this->ui->gameTreeView->model()->rowCount() > 0) {
        QModelIndex rootIndex = this->ui->gameTreeView->model()->index(0, 0);
        this->ui->gameTreeView->selectionModel()->setCurrentIndex(rootIndex, QItemSelectionModel::SelectCurrent);
        this->item_clicked(rootIndex);
    }
}

StrategyExplorer::~StrategyExplorer()
{
    if (gtoTrainerWindow) {
        gtoTrainerWindow->disconnect();
        delete gtoTrainerWindow;
        gtoTrainerWindow = nullptr;
    }
    delete ui;
    delete this->delegate_strategy;
    delete this->tableStrategyModel;
    delete this->detailViewerModel;
    delete this->roughStrategyViewerModel;
    delete this->timer;
}

void StrategyExplorer::item_expanded(const QModelIndex& index){
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    int num_child = item->childCount();
    for (int i = 0;i < num_child;i ++){
        TreeItem* one_child = item->child(i);
        if(one_child->childCount() != 0)continue;
        this->ui->gameTreeView->tree_model->reGenerateTreeItem(one_child->m_treedata.lock()->getRound(),one_child);
    }
}

void StrategyExplorer::process_board(TreeItem* treeitem){
    vector<string> board_str_arr = string_split(this->qSolverJob->board,',');
    vector<Card> cards;
    for(string one_board_str:board_str_arr){
        cards.push_back(Card(one_board_str));
    }
    if(treeitem != NULL){
        if(treeitem->m_treedata.lock()->getRound() == GameTreeNode::GameRound::TURN && !this->tableStrategyModel->getTrunCard().empty()){
            cards.push_back(Card(this->tableStrategyModel->getTrunCard()));
        }
        else if(treeitem->m_treedata.lock()->getRound() == GameTreeNode::GameRound::RIVER){
            if(!this->tableStrategyModel->getTrunCard().empty())
                cards.push_back(Card(this->tableStrategyModel->getTrunCard()));
            if(!this->tableStrategyModel->getRiverCard().empty())
                cards.push_back(Card(this->tableStrategyModel->getRiverCard()));
        }
    }
    this->ui->boardLabel->setText(QString("<b>%1: </b>").arg(tr("board")) + Card::boardCards2html(cards));
}

void StrategyExplorer::process_treeclick(TreeItem* treeitem){
    if(!treeitem) return;
    shared_ptr<GameTreeNode> treenode = treeitem->m_treedata.lock();
    this->selectedNode = treenode;
    QString node_id = QString::fromStdString(treenode->getNodeId());

    this->selectedGridRow = -1;
    this->selectedGridCol = -1;
    this->selectedComboRow = -1;
    this->selectedComboCol = -1;

    // Reset expression category selector
    this->ui->expressionSelectBox->blockSignals(true);
    this->ui->expressionSelectBox->setCurrentIndex(0);
    this->ui->expressionSelectBox->blockSignals(false);
    this->detailWindowSetting.hasExpressionHighlight = false;
    this->detailWindowSetting.selectedCategory.clear();
    memset(this->detailWindowSetting.cellHighlight, 0, sizeof(this->detailWindowSetting.cellHighlight));
    memset(this->detailWindowSetting.comboHighlight, 0, sizeof(this->detailWindowSetting.comboHighlight));

    if(treenode->getType() == GameTreeNode::GameTreeNodeType::ACTION){
        shared_ptr<ActionNode> actionnode = static_pointer_cast<ActionNode>(treenode);
        // Check lock status
        shared_ptr<Trainable> trainable = this->tableStrategyModel->get_current_trainable();
        bool locked = trainable && trainable->isLocked();
        QString lock_str = locked ? QString(" <font color='red'>[LOCKED]</font>") : "";
        QString action_str = QString("<b>%1 %2</b>%3<br><small>Node ID: %4</small>")
            .arg(actionnode->getPlayer() == 0?tr("IP"):tr("OOP"),
                 tr(" decision node"),
                 lock_str,
                 node_id);
        this->ui->nodeDisplayLabel->setText(action_str);

        std::vector<std::string> actionNames;
        for(auto& act : actionnode->getActions()) {
            actionNames.push_back(this->tableStrategyModel->formatActionName(act, actionnode).toStdString());
        }
        rebuildSliders(actionNames.size(), actionNames);
        this->ui->nodeLockEditorGroup->setEnabled(true);
        updateLockEditor();
    }
    else {
        this->ui->nodeLockEditorGroup->setEnabled(false);
        rebuildSliders(0, {});
        this->ui->lockEditorLabel->setText("Selected: [None]");
        if(treenode->getType() == GameTreeNode::GameTreeNodeType::CHANCE){
            QString chance_str = QString("<b>Chance node</b><br><small>Node ID: %1</small>").arg(node_id);
            this->ui->nodeDisplayLabel->setText(chance_str);
        }
        else if(treenode->getType() == GameTreeNode::GameTreeNodeType::TERMINAL){
            QString terminal_str = QString("<b>Terminal node</b><br><small>Node ID: %1</small>").arg(node_id);
            this->ui->nodeDisplayLabel->setText(terminal_str);
        }
        else if(treenode->getType() == GameTreeNode::GameTreeNodeType::SHOWDOWN){
            QString showdown_str = QString("<b>Showdown node</b><br><small>Node ID: %1</small>").arg(node_id);
            this->ui->nodeDisplayLabel->setText(showdown_str);
        }
    }
    updateLockButtons();
}

void StrategyExplorer::updateLockButtons(){
    bool is_action = selectedNode && selectedNode->getType() == GameTreeNode::GameTreeNodeType::ACTION;
    this->ui->dumpCsvButton->setEnabled(is_action);
    this->ui->lockCsvButton->setEnabled(is_action);

    if(is_action) {
        shared_ptr<ActionNode> actionnode = static_pointer_cast<ActionNode>(selectedNode);
        shared_ptr<Trainable> trainable = this->tableStrategyModel->get_current_trainable();
        bool locked = trainable && trainable->isLocked();
        this->ui->unlockButton->setEnabled(locked);
        this->ui->lockCsvButton->setText(locked ? "Re-Lock" : "Lock Node");
    } else {
        this->ui->unlockButton->setEnabled(false);
        this->ui->lockCsvButton->setText("Lock Node");
    }
}

void StrategyExplorer::item_clicked(const QModelIndex& index){
    try{
        TreeItem * treeNode = static_cast<TreeItem*>(index.internalPointer());
        this->tableStrategyModel->setGameTreeNode(treeNode);
        this->tableStrategyModel->updateStrategyData();
        this->process_treeclick(treeNode);
        this->process_board(treeNode);
        this->ui->strategyTableView->viewport()->update();
        this->roughStrategyViewerModel->onchanged();
        this->ui->roughStrategyView->triger_resize();
        this->ui->roughStrategyView->viewport()->update();
    }
    catch (const runtime_error& error)
    {
        qDebug().noquote() << tr("Encountering error:");//.toStdString() << endl;
        qDebug().noquote() << error.what() << "\n";
    }
}

void StrategyExplorer::selection_changed(const QItemSelection &selected,
                                         const QItemSelection &deselected){
}

void StrategyExplorer::on_turnCardBox_currentIndexChanged(int index)
{
    if(this->cards.size() > 0 && index < this->cards.size()){
        this->tableStrategyModel->setTrunCard(this->cards[index]);
        this->tableStrategyModel->updateStrategyData();
        // TODO this somehow cause bugs, crashes, why?
        //this->roughStrategyViewerModel->onchanged();
        //this->ui->roughStrategyView->viewport()->update();
        this->process_board(this->tableStrategyModel->treeItem);
        this->process_treeclick(this->tableStrategyModel->treeItem);
    }
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
}

void StrategyExplorer::on_riverCardBox_currentIndexChanged(int index)
{
    if(this->cards.size() > 0  && index < this->cards.size()){
        this->tableStrategyModel->setRiverCard(this->cards[index]);
        this->tableStrategyModel->updateStrategyData();
        //this->roughStrategyViewerModel->onchanged();
        //this->ui->roughStrategyView->viewport()->update();
        this->process_board(this->tableStrategyModel->treeItem);
        this->process_treeclick(this->tableStrategyModel->treeItem);
    }
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
}

void StrategyExplorer::update_second(){
    if(this->cards.size() > 0){
        this->tableStrategyModel->updateStrategyData();
        this->roughStrategyViewerModel->onchanged();
        this->ui->roughStrategyView->viewport()->update();
        this->process_board(this->tableStrategyModel->treeItem);
    }
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    updateSolvingStatus();
}

static QString formatDuration(qint64 ms) {
    qint64 totalSecs = ms / 1000;
    qint64 hours = totalSecs / 3600;
    qint64 minutes = (totalSecs % 3600) / 60;
    qint64 seconds = totalSecs % 60;
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    } else {
        return QString("%1:%2")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
}

void StrategyExplorer::updateSolvingStatus() {
    if (!solvingStatusLabel || !qSolverJob) return;

    // Try to get exploitability from the solver
    float expl = -1.0f;
    int iter = -1;
    PokerSolver* ps = qSolverJob->get_solver();
    if (ps) {
        auto solver = ps->get_solver();
        if (solver) {
            expl = solver->last_exploitability;
            iter = solver->last_iteration;
        }
    }
    QString explStr;
    if (expl >= 0.0f) {
        explStr = QString(" <span style='font-size:9px; color:#888;'>Expl: %1%</span>").arg(QString::number(expl, 'f', 2));
    }

    QString text = "";
    if (qSolverJob->isRunning()) {
        switch (qSolverJob->current_mission) {
            case QSolverJob::MissionType::SOLVING: {
                QString timeStr = "";
                if (qSolverJob->solveStartTime > 0) {
                    qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - qSolverJob->solveStartTime;
                    QString elapsed = formatDuration(elapsedMs);
                    QString estStr = "";

                    if (ps && iter > 0) {
                        float avg_speed_ms = (float)elapsedMs / iter;
                        
                        // Estimate remaining to max iterations
                        int remaining_iters = qSolverJob->max_iteration - iter;
                        float remaining_sec_by_iter = (remaining_iters > 0) ? (avg_speed_ms * remaining_iters / 1000.0f) : 0.0f;
                        
                        // Estimate remaining to target accuracy
                        float target_acc = qSolverJob->accuracy;
                        float remaining_sec_by_accuracy = -1.0f;
                        if (target_acc > 0.0f && expl > target_acc) {
                            float target_iters = iter * (expl / target_acc);
                            float remaining_iters_to_accuracy = target_iters - iter;
                            remaining_sec_by_accuracy = avg_speed_ms * remaining_iters_to_accuracy / 1000.0f;
                        }

                        if (remaining_sec_by_accuracy > 0.0f) {
                            estStr = QString(", remaining: %1 (accuracy) / %2 (max)")
                                .arg(formatDuration(static_cast<qint64>(remaining_sec_by_accuracy * 1000)))
                                .arg(formatDuration(static_cast<qint64>(remaining_sec_by_iter * 1000)));
                        } else if (remaining_sec_by_iter > 0.0f) {
                            estStr = QString(", remaining: %1 (max)")
                                .arg(formatDuration(static_cast<qint64>(remaining_sec_by_iter * 1000)));
                        }
                    }
                    timeStr = QString(" <span style='font-size:10px; color:#aaa;'>(%1%2)</span>").arg(elapsed).arg(estStr);
                }
                text = QString("<font color='#2ecc71'>●</font> <b>Solving...</b>%1%2").arg(timeStr).arg(explStr);
                break;
            }
            case QSolverJob::MissionType::LOADING:
                text = "<font color='#f1c40f'>●</font> <b>Loading...</b>";
                break;
            case QSolverJob::MissionType::SAVING:
                text = "<font color='#f1c40f'>●</font> <b>Saving...</b>";
                break;
            case QSolverJob::MissionType::BUILDTREE:
                text = "<font color='#f1c40f'>●</font> <b>Building Tree...</b>";
                break;
            default:
                text = "<font color='#3498db'>●</font> <b>Running...</b>";
                break;
        }
    } else {
        QString timeStr = "";
        if (qSolverJob->totalSolveTimeMs > 0) {
            timeStr = QString(" <span style='font-size:10px; color:#aaa;'>(took %1)</span>").arg(formatDuration(qSolverJob->totalSolveTimeMs));
        }
        text = QString("<font color='#7f8c8d'>●</font> <b>Stopped</b>%1%2").arg(timeStr).arg(explStr);
    }

    solvingStatusLabel->setText(text);

    // Dynamically update GTO Trainer button enabled status and tooltip
    bool enableTrainer = false;
    if (qSolverJob && !qSolverJob->isRunning() && expl >= 0.0f) {
        enableTrainer = true;
    }

    if (this->ui && this->ui->gtoTrainerButton) {
        this->ui->gtoTrainerButton->setEnabled(enableTrainer);
        if (enableTrainer) {
            this->ui->gtoTrainerButton->setToolTip(tr("Launch the GTO Trainer to practice this spot."));
        } else {
            if (qSolverJob && qSolverJob->isRunning()) {
                this->ui->gtoTrainerButton->setToolTip(tr("GTO Trainer is disabled because the solver is currently running."));
            } else if (expl < 0.0f) {
                this->ui->gtoTrainerButton->setToolTip(tr("GTO Trainer is disabled because the tree has not been solved yet."));
            }
        }
    }
}


void StrategyExplorer::onMouseMoveEvent(int i,int j){
    this->detailWindowSetting.grid_i = i;
    this->detailWindowSetting.grid_j = j;

    // Update combo highlights for the hovered cell if expression category is active
    if (this->detailWindowSetting.hasExpressionHighlight) {
        updateComboHighlightsForCell(i, j);
    }

    this->ui->detailView->viewport()->update();
    this->ui->strategyTableView->viewport()->update();
}

void StrategyExplorer::on_strategyModeButtom_clicked()
{
    this->detailWindowSetting.mode = DetailWindowSetting::DetailWindowMode::STRATEGY;
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
}

void StrategyExplorer::on_ipRangeButtom_clicked()
{
    this->detailWindowSetting.mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
}

void StrategyExplorer::on_oopRangeButtom_clicked()
{
    this->detailWindowSetting.mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
}

void StrategyExplorer::on_evModeButtom_clicked()
{
    this->detailWindowSetting.mode = DetailWindowSetting::DetailWindowMode::EV;
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    this->ui->roughStrategyView->viewport()->update();
}

void StrategyExplorer::on_evOnlyModeButtom_clicked()
{
    this->detailWindowSetting.mode = DetailWindowSetting::DetailWindowMode::EV_ONLY;
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
}

void StrategyExplorer::on_dumpCsvButton_clicked()
{
    if(!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;

    shared_ptr<ActionNode> action_node = static_pointer_cast<ActionNode>(selectedNode);
    shared_ptr<Trainable> trainable = this->tableStrategyModel->get_current_trainable();
    if(!trainable) {
        QMessageBox::warning(this, "Error", "Trainable not initialized. Run the solver first.");
        return;
    }

    QString filename = QFileDialog::getSaveFileName(this, "Save Strategy CSV", "", "CSV Files (*.csv)");
    if(filename.isEmpty()) return;

    const vector<float>& avg_strategy = trainable->getAverageStrategy();
    vector<GameActions>& actions = action_node->getActions();
    vector<PrivateCards>* private_cards = action_node->player_privates;

    if(!private_cards || private_cards->empty()) {
        QMessageBox::warning(this, "Error", "No private cards data. Run the solver first.");
        return;
    }

    int num_actions = actions.size();
    int num_hands = private_cards->size();

    ofstream out(filename.toStdString());
    // Header
    out << "hand";
    for(int a = 0; a < num_actions; a++) {
        out << "," << actions[a].toString();
    }
    out << endl;

    // Data rows
    for(int h = 0; h < num_hands; h++) {
        out << (*private_cards)[h].toString();
        for(int a = 0; a < num_actions; a++) {
            int idx = a * num_hands + h;
            float val = (idx < (int)avg_strategy.size()) ? avg_strategy[idx] : 0.0f;
            out << "," << val;
        }
        out << endl;
    }
    out.close();

    qDebug().noquote() << QString("Strategy dumped to %1 (%2 hands x %3 actions)")
        .arg(filename).arg(num_hands).arg(num_actions);
}

void StrategyExplorer::on_lockCsvButton_clicked()
{
    if(!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;

    shared_ptr<ActionNode> action_node = static_pointer_cast<ActionNode>(selectedNode);
    shared_ptr<Trainable> trainable = this->tableStrategyModel->get_current_trainable();
    if(!trainable) {
        QMessageBox::warning(this, "Error", "Trainable not initialized. Run the solver first.");
        return;
    }

    QString filename = QFileDialog::getOpenFileName(this, "Load Strategy CSV", "", "CSV Files (*.csv)");
    if(filename.isEmpty()) return;

    ifstream in(filename.toStdString());
    if(!in.is_open()) {
        QMessageBox::warning(this, "Error", "Cannot open file: " + filename);
        return;
    }

    vector<GameActions>& actions = action_node->getActions();
    vector<PrivateCards>* private_cards = action_node->player_privates;
    int num_actions = actions.size();
    int num_hands = private_cards->size();

    // Build hand-to-index map
    map<string, int> hand_index;
    for(int h = 0; h < num_hands; h++) {
        hand_index[(*private_cards)[h].toString()] = h;
    }

    vector<float> strategy(num_actions * num_hands, 0.0f);
    string line;
    getline(in, line); // skip header

    int loaded = 0;
    while(getline(in, line)) {
        stringstream ss(line);
        string hand;
        getline(ss, hand, ',');

        auto it = hand_index.find(hand);
        if(it == hand_index.end()) continue;
        int h = it->second;

        for(int a = 0; a < num_actions; a++) {
            string val_str;
            getline(ss, val_str, ',');
            float val = stof(val_str);
            strategy[a * num_hands + h] = val;
        }
        loaded++;
    }
    in.close();

    std::vector<bool> mask(num_hands, true);
    trainable->lockStrategy(strategy, mask);

    // NEW
    int deal_index = action_node->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);
    qSolverJob->storeLockedStrategy(key.toStdString(), strategy, mask);

    qDebug().noquote() << QString("Node locked with %1 hands from %2").arg(loaded).arg(filename);


    qDebug().noquote() << QString("There are currently %1 locked nodes").arg(qSolverJob->locked_strategies.size());
    updateLockButtons();

    // Refresh display
    this->ui->strategyTableView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
    this->ui->gameTreeView->viewport()->update();
}
void StrategyExplorer::on_unlockButton_clicked()
{
    if(!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;

    shared_ptr<ActionNode> action_node = static_pointer_cast<ActionNode>(selectedNode);
    shared_ptr<Trainable> trainable = this->tableStrategyModel->get_current_trainable();
    if(!trainable) return;

    trainable->unlockStrategy();

    // NEW
    int deal_index = action_node->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);
    qSolverJob->locked_strategies.erase(key.toStdString());

    qDebug().noquote() << "Node unlocked.";
    updateLockButtons();

    // Re-read the node display to remove [LOCKED]
    QString node_id = QString::fromStdString(selectedNode->getNodeId());
    QString action_str = QString("<b>%1 %2</b><br><small>Node ID: %3</small>")
        .arg(action_node->getPlayer() == 0?tr("IP"):tr("OOP"),
             tr(" decision node"),
             node_id);
    this->ui->nodeDisplayLabel->setText(action_str);

    updateLockEditor();

    this->ui->strategyTableView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
    this->ui->gameTreeView->viewport()->update();
}

void StrategyExplorer::onStrategyGridClicked(const QModelIndex &index) {
    if (!index.isValid()) return;

    if (painterModeActive) {
        // Painter mode: paint or erase immediately on click
        int gridRow = index.row();
        int gridCol = index.column();
        std::vector<float> brush = getBrushStrategy();
        if (isCellLockedWithStrategy(gridRow, gridCol, brush)) {
            erasePaintCell(gridRow, gridCol);
        } else {
            paintCell(gridRow, gridCol);
        }
        return;
    }

    // Normal mode: select/deselect cell
    if (this->selectedGridRow == index.row() && this->selectedGridCol == index.column()) {
        this->selectedGridRow = -1;
        this->selectedGridCol = -1;
        this->selectedComboRow = -1;
        this->selectedComboCol = -1;
    } else {
        this->selectedGridRow = index.row();
        this->selectedGridCol = index.column();
        this->selectedComboRow = -1;
        this->selectedComboCol = -1;
    }
    updateLockEditor();
}

void StrategyExplorer::onDetailViewClicked(const QModelIndex &index) {
    if (!index.isValid()) return;

    if (painterModeActive) {
        // Painter mode: paint or erase combo immediately on click
        int comboRow = index.row();
        int comboCol = index.column();

        // Synchronize selectedGridRow/Col to the currently displayed cell in detailView
        this->selectedGridRow = detailWindowSetting.grid_i;
        this->selectedGridCol = detailWindowSetting.grid_j;

        std::vector<float> brush = getBrushStrategy();
        if (isComboLockedWithStrategy(comboRow, comboCol, brush)) {
            erasePaintCombo(comboRow, comboCol);
        } else {
            paintCombo(comboRow, comboCol);
        }
        return;
    }

    // Normal mode: select/deselect combo
    if (this->selectedComboRow == index.row() && this->selectedComboCol == index.column()) {
        this->selectedGridRow = -1;
        this->selectedGridCol = -1;
        this->selectedComboRow = -1;
        this->selectedComboCol = -1;
    } else {
        this->selectedComboRow = index.row();
        this->selectedComboCol = index.column();
    }
    updateLockEditor();
}

void StrategyExplorer::rebuildSliders(int actionCount, const std::vector<std::string>& actionNames) {
    std::function<void(QLayout*)> clearLayout = [&](QLayout* layout) {
        if (!layout) return;
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            } else if (QLayout* subLayout = item->layout()) {
                clearLayout(subLayout);
                delete subLayout;
            } else {
                delete item;
            }
        }
    };

    clearLayout(this->ui->lockSlidersLayout);
    lockSliders.clear();
    lockSliderLabels.clear();

    int initialVal = actionCount > 0 ? 100 / actionCount : 0;
    int remainder = actionCount > 0 ? 100 % actionCount : 0;

    for (int a = 0; a < actionCount; ++a) {
        QVBoxLayout* itemLayout = new QVBoxLayout();
        
        QLabel* nameLabel = new QLabel(QString::fromStdString(actionNames[a]), this);
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setStyleSheet("font-size: 10px; font-weight: bold;");
        itemLayout->addWidget(nameLabel);

        QSlider* slider = new QSlider(Qt::Vertical, this);
        slider->setRange(0, 100);
        slider->setValue(initialVal + (a == 0 ? remainder : 0));
        slider->setFixedHeight(70);
        connect(slider, &QSlider::valueChanged, this, &StrategyExplorer::onSliderValueChanged);
        itemLayout->addWidget(slider);
        lockSliders.push_back(slider);

        QDoubleSpinBox* spinbox = new QDoubleSpinBox(this);
        spinbox->setRange(0.0, 1.0);
        spinbox->setSingleStep(0.01);
        spinbox->setDecimals(2);
        spinbox->setValue(slider->value() / 100.0);
        spinbox->setAlignment(Qt::AlignCenter);
        spinbox->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spinbox->setFixedWidth(42);
        spinbox->setStyleSheet("font-size: 10px; padding: 1px;");
        connect(spinbox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &StrategyExplorer::onSpinBoxValueChanged);
        itemLayout->addWidget(spinbox, 0, Qt::AlignCenter);
        lockSliderLabels.push_back(spinbox);

        this->ui->lockSlidersLayout->addLayout(itemLayout);
    }
}

void StrategyExplorer::updateSliderValues(const std::vector<float>& strategy) {
    isUpdatingSliders = true;
    for (size_t a = 0; a < lockSliders.size(); ++a) {
        if (a < strategy.size()) {
            int val = qRound(strategy[a] * 100.0);
            lockSliders[a]->setValue(val);
            lockSliderLabels[a]->setValue(val / 100.0);
        }
    }
    isUpdatingSliders = false;
}

void StrategyExplorer::onSliderValueChanged(int value) {
    if (isUpdatingSliders) return;
    QSlider* senderSlider = qobject_cast<QSlider*>(sender());
    if (!senderSlider) return;

    int senderIndex = -1;
    for (size_t i = 0; i < lockSliders.size(); ++i) {
        if (lockSliders[i] == senderSlider) {
            senderIndex = i;
            break;
        }
    }
    if (senderIndex == -1) return;

    isUpdatingSliders = true;

    int totalRemaining = 100 - value;
    int currentOthersSum = 0;
    for (size_t i = 0; i < lockSliders.size(); ++i) {
        if (i != senderIndex) {
            currentOthersSum += lockSliders[i]->value();
        }
    }

    if (currentOthersSum > 0) {
        for (size_t i = 0; i < lockSliders.size(); ++i) {
            if (i != senderIndex) {
                double ratio = (double)lockSliders[i]->value() / currentOthersSum;
                lockSliders[i]->setValue(qRound(ratio * totalRemaining));
            }
        }
    } else {
        int count = lockSliders.size() - 1;
        if (count > 0) {
            int equalVal = totalRemaining / count;
            for (size_t i = 0; i < lockSliders.size(); ++i) {
                if (i != senderIndex) {
                    lockSliders[i]->setValue(equalVal);
                }
            }
        }
    }

    for (size_t i = 0; i < lockSliders.size(); ++i) {
        lockSliderLabels[i]->setValue(lockSliders[i]->value() / 100.0);
    }

    isUpdatingSliders = false;
}

void StrategyExplorer::onSpinBoxValueChanged(double val) {
    if (isUpdatingSliders) return;
    QDoubleSpinBox* senderSpin = qobject_cast<QDoubleSpinBox*>(sender());
    if (!senderSpin) return;

    int senderIndex = -1;
    for (size_t i = 0; i < lockSliderLabels.size(); ++i) {
        if (lockSliderLabels[i] == senderSpin) {
            senderIndex = i;
            break;
        }
    }
    if (senderIndex == -1) return;

    int sliderVal = qRound(val * 100.0);
    if (lockSliders[senderIndex]->value() != sliderVal) {
        lockSliders[senderIndex]->setValue(sliderVal);
    }
}

void StrategyExplorer::updateLockEditor() {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) {
        this->ui->nodeLockEditorGroup->setEnabled(false);
        return;
    }

    this->ui->nodeLockEditorGroup->setEnabled(true);

    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !actionnode->player_privates) {
        this->ui->nodeLockEditorGroup->setEnabled(false);
        return;
    }

    int num_hands = actionnode->player_privates->size();
    int num_actions = actionnode->getActions().size();

    bool categoryActive = detailWindowSetting.hasExpressionHighlight;
    if (categoryActive) {
        QString catName = detailWindowSetting.selectedCategory;
        bool isCurrentlyLocked = trainable->isLocked();
        
        if (painterModeActive) {
            this->ui->lockSelectionButton->setEnabled(false);
            this->ui->unlockSelectionButton->setEnabled(false);
            for (auto* s : lockSliders) s->setEnabled(true);
            this->ui->lockEditorLabel->setText(QString("Selected: Category <b>%1</b> (Painter Mode Active)").arg(catName));
            return;
        }

        this->ui->lockEditorLabel->setText(QString("Selected: Category <b>%1</b> %2")
            .arg(catName, isCurrentlyLocked ? "<font color='red'>[MAYBE LOCKED]</font>" : ""));

        this->ui->lockSelectionButton->setEnabled(true);
        this->ui->unlockSelectionButton->setEnabled(isCurrentlyLocked);
        for (auto* s : lockSliders) s->setEnabled(true);
        return;
    }

    if (selectedGridRow < 0 || selectedGridCol < 0) {
        this->ui->lockEditorLabel->setText("Selected: [None] (Click starting hand grid to lock)");
        this->ui->lockSelectionButton->setEnabled(false);
        this->ui->unlockSelectionButton->setEnabled(false);
        
        // Keep sliders enabled so they can configure strategy before selection/painting!
        for (size_t i = 0; i < lockSliders.size(); ++i) {
            lockSliders[i]->setEnabled(true);
        }
        return;
    }

    auto deck = tableStrategyModel->get_solver()->get_solver()->get_deck();
    auto ranks = deck->getRanks();
    int larger = std::max(selectedGridRow, selectedGridCol);
    int smaller = std::min(selectedGridRow, selectedGridCol);
    QString typeStr = "";
    if (selectedGridRow > selectedGridCol) typeStr = "o";
    else if (selectedGridRow < selectedGridCol) typeStr = "s";
    QString handName = QString("%1%2%3")
        .arg(QString::fromStdString(ranks[ranks.size() - 1 - smaller]))
        .arg(QString::fromStdString(ranks[ranks.size() - 1 - larger]))
        .arg(typeStr);

    const auto& combos = tableStrategyModel->ui_strategy_table[selectedGridRow][selectedGridCol];
    if (combos.empty()) {
        this->ui->lockEditorLabel->setText("Selected: [None] (Empty starting hand cell)");
        this->ui->lockSelectionButton->setEnabled(false);
        this->ui->unlockSelectionButton->setEnabled(false);
        for (size_t i = 0; i < lockSliders.size(); ++i) {
            lockSliders[i]->setEnabled(true);
        }
        return;
    }

    bool isComboSelected = (selectedComboRow >= 0 && selectedComboCol >= 0);
    int selected_combo_idx = -1;
    if (isComboSelected) {
        selected_combo_idx = selectedComboRow * 4 + selectedComboCol;
    }

    QString selectionName = "";
    bool isCurrentlyLocked = false;
    std::vector<float> avgStrategy(num_actions, 0.0f);

    if (isComboSelected && selected_combo_idx < combos.size()) {
        auto combo = combos[selected_combo_idx];
        int card1 = combo.first;
        int card2 = combo.second;
        QString comboName = QString::fromStdString(tableStrategyModel->cardint2card[card1].toString() + tableStrategyModel->cardint2card[card2].toString());
        selectionName = QString("Combo %1").arg(comboName);

        int private_id = -1;
        if (actionnode->player_privates) {
            for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
                const auto& pc = (*actionnode->player_privates)[k];
                if ((pc.card1 == card1 && pc.card2 == card2) || (pc.card1 == card2 && pc.card2 == card1)) {
                    private_id = k;
                    break;
                }
            }
        }

        if (private_id >= 0) {
            if (trainable->isLocked() && private_id < trainable->getLockedMask().size()) {
                isCurrentlyLocked = trainable->getLockedMask()[private_id];
            }
            for (int a = 0; a < num_actions; ++a) {
                avgStrategy[a] = tableStrategyModel->current_strategy[card1][card2][a];
            }
        }
    } else {
        selectionName = QString("Hand %1").arg(handName);
        
        int valid_combos = 0;
        for (const auto& combo : combos) {
            int card1 = combo.first;
            int card2 = combo.second;
            if (card1 < tableStrategyModel->current_strategy.size() && card2 < tableStrategyModel->current_strategy[card1].size()) {
                for (int a = 0; a < num_actions; ++a) {
                    avgStrategy[a] += tableStrategyModel->current_strategy[card1][card2][a];
                }
                valid_combos++;
            }
        }
        if (valid_combos > 0) {
            for (int a = 0; a < num_actions; ++a) {
                avgStrategy[a] /= valid_combos;
            }
        }

        if (trainable->isLocked() && actionnode->player_privates) {
            bool allLocked = true;
            for (const auto& combo : combos) {
                int private_id = -1;
                for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
                    const auto& pc = (*actionnode->player_privates)[k];
                    if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
                        private_id = k;
                        break;
                    }
                }
                if (private_id < 0 || !trainable->getLockedMask()[private_id]) {
                    allLocked = false;
                    break;
                }
            }
            isCurrentlyLocked = allLocked;
        }
    }

    if (painterModeActive) {
        // In painter mode, the sliders are the brush strategy. DO NOT overwrite them with the selection's strategy!
        this->ui->lockSelectionButton->setEnabled(false);
        this->ui->unlockSelectionButton->setEnabled(false);
        for (auto* s : lockSliders) s->setEnabled(true);
        this->ui->lockEditorLabel->setText(QString("Selected: <b>%1</b> (Painter Mode Active)")
            .arg(selectionName));
        return;
    }

    this->ui->lockEditorLabel->setText(QString("Selected: <b>%1</b> %2")
        .arg(selectionName, isCurrentlyLocked ? "<font color='red'>[LOCKED]</font>" : ""));

    this->ui->lockSelectionButton->setEnabled(true);
    this->ui->unlockSelectionButton->setEnabled(isCurrentlyLocked);
    for (auto* s : lockSliders) s->setEnabled(true);

    updateSliderValues(avgStrategy);
}

void StrategyExplorer::on_lockSelectionButton_clicked() {
    applyLockToSelection();
}

void StrategyExplorer::applyLockToSelection() {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;

    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !actionnode->player_privates) return;

    int num_hands = actionnode->player_privates->size();
    int num_actions = actionnode->getActions().size();

    bool categoryActive = detailWindowSetting.hasExpressionHighlight;
    if (!categoryActive && (selectedGridRow < 0 || selectedGridCol < 0)) return;

    bool isComboSelected = (selectedComboRow >= 0 && selectedComboCol >= 0);
    int selected_combo_idx = -1;
    if (isComboSelected) {
        selected_combo_idx = selectedComboRow * 4 + selectedComboCol;
    }

    std::vector<float> new_strategy = trainable->getAverageStrategy();
    std::vector<bool> new_mask(num_hands, false);

    int deal_index = actionnode->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);

    if (trainable->isLocked()) {
        auto it = qSolverJob->locked_strategies.find(key.toStdString());
        if (it != qSolverJob->locked_strategies.end()) {
            new_strategy = it->second.strategy;
            new_mask = it->second.mask;
        } else {
            new_mask = trainable->getLockedMask();
        }
    }

    std::vector<float> input_values(num_actions);
    for (int a = 0; a < num_actions; ++a) {
        input_values[a] = (float)lockSliders[a]->value() / 100.0f;
    }
    float sum = 0.0f;
    for (float v : input_values) sum += v;
    if (sum > 0.001f) {
        for (float& v : input_values) v /= sum;
    } else {
        input_values[0] = 1.0f;
    }

    auto applyToCombo = [&](int c1, int c2) {
        int private_id = -1;
        if (actionnode->player_privates) {
            for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
                const auto& pc = (*actionnode->player_privates)[k];
                if ((pc.card1 == c1 && pc.card2 == c2) || (pc.card1 == c2 && pc.card2 == c1)) {
                    private_id = k;
                    break;
                }
            }
        }
        if (private_id >= 0) {
            new_mask[private_id] = true;
            for (int a = 0; a < num_actions; ++a) {
                new_strategy[a * num_hands + private_id] = input_values[a];
            }
        }
    };

    if (categoryActive) {
        std::vector<int> boardInts = getCurrentBoardCardInts();
        for (int r = 0; r < 13 && r < (int)tableStrategyModel->ui_strategy_table.size(); r++) {
            for (int c = 0; c < 13 && c < (int)tableStrategyModel->ui_strategy_table[r].size(); c++) {
                const auto& combos = tableStrategyModel->ui_strategy_table[r][c];
                for (const auto& combo : combos) {
                    if (comboMatchesCategory(combo.first, combo.second, boardInts, detailWindowSetting.selectedCategory)) {
                        applyToCombo(combo.first, combo.second);
                    }
                }
            }
        }
    } else {
        const auto& combos = tableStrategyModel->ui_strategy_table[selectedGridRow][selectedGridCol];
        if (!combos.empty()) {
            if (isComboSelected && selected_combo_idx < combos.size()) {
                applyToCombo(combos[selected_combo_idx].first, combos[selected_combo_idx].second);
            } else {
                for (const auto& combo : combos) {
                    applyToCombo(combo.first, combo.second);
                }
            }
        }
    }

    trainable->lockStrategy(new_strategy, new_mask);
    qSolverJob->storeLockedStrategy(key.toStdString(), new_strategy, new_mask);

    // Refresh display
    QString node_id_str = QString::fromStdString(selectedNode->getNodeId());
    QString action_str = QString("<b>%1 %2</b> <font color='red'>[LOCKED]</font><br><small>Node ID: %3</small>")
        .arg(actionnode->getPlayer() == 0?tr("IP"):tr("OOP"),
             tr(" decision node"),
             node_id_str);
    this->ui->nodeDisplayLabel->setText(action_str);

    updateLockEditor();
    updateLockButtons();

    this->tableStrategyModel->updateStrategyData();
    this->ui->strategyTableView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
    this->ui->gameTreeView->viewport()->update();
}

void StrategyExplorer::on_unlockSelectionButton_clicked() {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;

    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !actionnode->player_privates) return;

    bool categoryActive = detailWindowSetting.hasExpressionHighlight;
    if (!categoryActive && (selectedGridRow < 0 || selectedGridCol < 0)) return;

    bool isComboSelected = (selectedComboRow >= 0 && selectedComboCol >= 0);
    int selected_combo_idx = -1;
    if (isComboSelected) {
        selected_combo_idx = selectedComboRow * 4 + selectedComboCol;
    }

    int deal_index = actionnode->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);

    if (!trainable->isLocked()) return;

    std::vector<float> strategy = trainable->getAverageStrategy();
    std::vector<bool> mask = trainable->getLockedMask();

    auto it = qSolverJob->locked_strategies.find(key.toStdString());
    if (it != qSolverJob->locked_strategies.end()) {
        strategy = it->second.strategy;
        mask = it->second.mask;
    }

    auto unlockCombo = [&](int c1, int c2) {
        int private_id = -1;
        if (actionnode->player_privates) {
            for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
                const auto& pc = (*actionnode->player_privates)[k];
                if ((pc.card1 == c1 && pc.card2 == c2) || (pc.card1 == c2 && pc.card2 == c1)) {
                    private_id = k;
                    break;
                }
            }
        }
        if (private_id >= 0) {
            mask[private_id] = false;
        }
    };

    if (categoryActive) {
        std::vector<int> boardInts = getCurrentBoardCardInts();
        for (int r = 0; r < 13 && r < (int)tableStrategyModel->ui_strategy_table.size(); r++) {
            for (int c = 0; c < 13 && c < (int)tableStrategyModel->ui_strategy_table[r].size(); c++) {
                const auto& combos = tableStrategyModel->ui_strategy_table[r][c];
                for (const auto& combo : combos) {
                    if (comboMatchesCategory(combo.first, combo.second, boardInts, detailWindowSetting.selectedCategory)) {
                        unlockCombo(combo.first, combo.second);
                    }
                }
            }
        }
    } else {
        const auto& combos = tableStrategyModel->ui_strategy_table[selectedGridRow][selectedGridCol];
        if (!combos.empty()) {
            if (isComboSelected && selected_combo_idx < combos.size()) {
                unlockCombo(combos[selected_combo_idx].first, combos[selected_combo_idx].second);
            } else {
                for (const auto& combo : combos) {
                    unlockCombo(combo.first, combo.second);
                }
            }
        }
    }

    bool hasAnyTrue = false;
    for (bool b : mask) {
        if (b) {
            hasAnyTrue = true;
            break;
        }
    }

    if (hasAnyTrue) {
        trainable->lockStrategy(strategy, mask);
        qSolverJob->storeLockedStrategy(key.toStdString(), strategy, mask);
    } else {
        trainable->unlockStrategy();
        qSolverJob->locked_strategies.erase(key.toStdString());
    }


    // Refresh display
    bool locked = trainable->isLocked();
    QString node_id_str = QString::fromStdString(selectedNode->getNodeId());
    QString action_str;
    if (locked) {
        action_str = QString("<b>%1 %2</b> <font color='red'>[LOCKED]</font><br><small>Node ID: %3</small>")
            .arg(actionnode->getPlayer() == 0?tr("IP"):tr("OOP"),
                 tr(" decision node"),
                 node_id_str);
    } else {
        action_str = QString("<b>%1 %2</b><br><small>Node ID: %3</small>")
            .arg(actionnode->getPlayer() == 0?tr("IP"):tr("OOP"),
                 tr(" decision node"),
                 node_id_str);
    }
    this->ui->nodeDisplayLabel->setText(action_str);

    updateLockEditor();
    updateLockButtons();

    this->tableStrategyModel->updateStrategyData();
    this->ui->strategyTableView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
    this->ui->gameTreeView->viewport()->update();
}

// ==================== Strategy Painter Tool ====================

void StrategyExplorer::on_painterModeButton_toggled(bool checked) {
    painterModeActive = checked;
    if (checked) {
        this->ui->painterModeButton->setStyleSheet("QPushButton { background-color: #ff6b6b; color: white; font-weight: bold; }");
        this->setCursor(Qt::CrossCursor);
    } else {
        this->ui->painterModeButton->setStyleSheet("");
        this->setCursor(Qt::ArrowCursor);
    }
}

bool StrategyExplorer::isCellFullyLocked(int gridRow, int gridCol) {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return false;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !trainable->isLocked() || !actionnode->player_privates) return false;

    if (gridRow < 0 || gridCol < 0) return false;
    const auto& combos = tableStrategyModel->ui_strategy_table[gridRow][gridCol];
    if (combos.empty()) return false;

    const auto& mask = trainable->getLockedMask();
    for (const auto& combo : combos) {
        int private_id = -1;
        for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
            const auto& pc = (*actionnode->player_privates)[k];
            if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
                private_id = k;
                break;
            }
        }
        if (private_id < 0 || private_id >= (int)mask.size() || !mask[private_id]) {
            return false;
        }
    }
    return true;
}

bool StrategyExplorer::isComboLocked(int comboRow, int comboCol) {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return false;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !trainable->isLocked() || !actionnode->player_privates) return false;
    if (selectedGridRow < 0 || selectedGridCol < 0) return false;

    const auto& combos = tableStrategyModel->ui_strategy_table[selectedGridRow][selectedGridCol];
    int combo_idx = comboRow * 4 + comboCol;
    if (combo_idx < 0 || combo_idx >= (int)combos.size()) return false;

    auto combo = combos[combo_idx];
    const auto& mask = trainable->getLockedMask();
    for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
        const auto& pc = (*actionnode->player_privates)[k];
        if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
            return k < mask.size() && mask[k];
        }
    }
    return false;
}

void StrategyExplorer::paintCell(int gridRow, int gridCol) {
    // Temporarily set the selection to the target cell (no combo selection)
    this->selectedGridRow = gridRow;
    this->selectedGridCol = gridCol;
    this->selectedComboRow = -1;
    this->selectedComboCol = -1;
    applyLockToSelection();
}

void StrategyExplorer::paintCombo(int comboRow, int comboCol) {
    // selectedGridRow/Col should already be set from a previous grid click
    this->selectedComboRow = comboRow;
    this->selectedComboCol = comboCol;
    applyLockToSelection();
}

void StrategyExplorer::erasePaintCell(int gridRow, int gridCol) {
    this->selectedGridRow = gridRow;
    this->selectedGridCol = gridCol;
    this->selectedComboRow = -1;
    this->selectedComboCol = -1;

    // Temporarily call unlock logic
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !trainable->isLocked() || !actionnode->player_privates) return;

    int deal_index = actionnode->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);

    std::vector<float> strategy = trainable->getAverageStrategy();
    std::vector<bool> mask = trainable->getLockedMask();

    auto it = qSolverJob->locked_strategies.find(key.toStdString());
    if (it != qSolverJob->locked_strategies.end()) {
        strategy = it->second.strategy;
        mask = it->second.mask;
    }

    const auto& combos = tableStrategyModel->ui_strategy_table[gridRow][gridCol];
    for (const auto& combo : combos) {
        for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
            const auto& pc = (*actionnode->player_privates)[k];
            if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
                if (k < mask.size()) mask[k] = false;
                break;
            }
        }
    }

    bool hasAnyTrue = false;
    for (bool b : mask) { if (b) { hasAnyTrue = true; break; } }

    if (hasAnyTrue) {
        trainable->lockStrategy(strategy, mask);
        qSolverJob->storeLockedStrategy(key.toStdString(), strategy, mask);
    } else {
        trainable->unlockStrategy();
        qSolverJob->locked_strategies.erase(key.toStdString());
    }

    // Refresh display
    bool locked = trainable->isLocked();
    QString node_id_str = QString::fromStdString(selectedNode->getNodeId());
    QString action_str;
    if (locked) {
        action_str = QString("<b>%1 %2</b> <font color='red'>[LOCKED]</font><br><small>Node ID: %3</small>")
            .arg(actionnode->getPlayer() == 0?tr("IP"):tr("OOP"), tr(" decision node"), node_id_str);
    } else {
        action_str = QString("<b>%1 %2</b><br><small>Node ID: %3</small>")
            .arg(actionnode->getPlayer() == 0?tr("IP"):tr("OOP"), tr(" decision node"), node_id_str);
    }
    this->ui->nodeDisplayLabel->setText(action_str);
    updateLockEditor();
    updateLockButtons();
    this->tableStrategyModel->updateStrategyData();
    this->ui->strategyTableView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
    this->ui->gameTreeView->viewport()->update();
}

void StrategyExplorer::erasePaintCombo(int comboRow, int comboCol) {
    this->selectedComboRow = comboRow;
    this->selectedComboCol = comboCol;

    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !trainable->isLocked() || !actionnode->player_privates) return;
    if (selectedGridRow < 0 || selectedGridCol < 0) return;

    int deal_index = actionnode->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);

    std::vector<float> strategy = trainable->getAverageStrategy();
    std::vector<bool> mask = trainable->getLockedMask();

    auto it = qSolverJob->locked_strategies.find(key.toStdString());
    if (it != qSolverJob->locked_strategies.end()) {
        strategy = it->second.strategy;
        mask = it->second.mask;
    }

    const auto& combos = tableStrategyModel->ui_strategy_table[selectedGridRow][selectedGridCol];
    int combo_idx = comboRow * 4 + comboCol;
    if (combo_idx >= 0 && combo_idx < (int)combos.size()) {
        auto combo = combos[combo_idx];
        for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
            const auto& pc = (*actionnode->player_privates)[k];
            if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
                if (k < mask.size()) mask[k] = false;
                break;
            }
        }
    }

    bool hasAnyTrue = false;
    for (bool b : mask) { if (b) { hasAnyTrue = true; break; } }

    if (hasAnyTrue) {
        trainable->lockStrategy(strategy, mask);
        qSolverJob->storeLockedStrategy(key.toStdString(), strategy, mask);
    } else {
        trainable->unlockStrategy();
        qSolverJob->locked_strategies.erase(key.toStdString());
    }

    updateLockEditor();
    updateLockButtons();
    this->tableStrategyModel->updateStrategyData();
    this->ui->strategyTableView->viewport()->update();
    this->roughStrategyViewerModel->onchanged();
    this->ui->roughStrategyView->viewport()->update();
    this->ui->gameTreeView->viewport()->update();
}

void StrategyExplorer::on_gtoTrainerButton_clicked()
{
    if (!qSolverJob || !qSolverJob->get_solver()) return;
    if (gtoTrainerWindow) {
        gtoTrainerWindow->raise();
        gtoTrainerWindow->activateWindow();
        return;
    }
    gtoTrainerWindow = new GtoTrainerWindow(this, qSolverJob, selectedNode);
    gtoTrainerWindow->setAttribute(Qt::WA_DeleteOnClose);
    connect(gtoTrainerWindow, &QObject::destroyed, this, [this]() {
        gtoTrainerWindow = nullptr;
    });
    gtoTrainerWindow->showMaximized();
}



void StrategyExplorer::setPreselectedCards(const QString& turn, const QString& river)
{
    if (!turn.isEmpty()) {
        QString fmtTurn = QString::fromStdString(Card(turn.toStdString()).toFormattedString());
        int idx = ui->turnCardBox->findText(fmtTurn, Qt::MatchExactly);
        if (idx == -1) idx = ui->turnCardBox->findText(fmtTurn, Qt::MatchContains);
        qDebug() << "setPreselectedCards turn:" << turn << " fmtTurn:" << fmtTurn << " idx:" << idx;
        if (idx >= 0) ui->turnCardBox->setCurrentIndex(idx);
    }
    if (!river.isEmpty()) {
        QString fmtRiver = QString::fromStdString(Card(river.toStdString()).toFormattedString());
        int idx = ui->riverCardBox->findText(fmtRiver, Qt::MatchExactly);
        if (idx == -1) idx = ui->riverCardBox->findText(fmtRiver, Qt::MatchContains);
        qDebug() << "setPreselectedCards river:" << river << " fmtRiver:" << fmtRiver << " idx:" << idx;
        if (idx >= 0) ui->riverCardBox->setCurrentIndex(idx);
    }
}

std::vector<float> StrategyExplorer::getBrushStrategy() {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return {};
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    int num_actions = actionnode->getActions().size();
    std::vector<float> input_values(num_actions);
    for (int a = 0; a < num_actions; ++a) {
        input_values[a] = (float)lockSliders[a]->value() / 100.0f;
    }
    float sum = 0.0f;
    for (float v : input_values) sum += v;
    if (sum > 0.001f) {
        for (float& v : input_values) v /= sum;
    } else {
        input_values[0] = 1.0f;
    }
    return input_values;
}

bool StrategyExplorer::isCellLockedWithStrategy(int gridRow, int gridCol, const std::vector<float>& brushStrategy) {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return false;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !trainable->isLocked() || !actionnode->player_privates) return false;

    if (gridRow < 0 || gridCol < 0) return false;
    const auto& combos = tableStrategyModel->ui_strategy_table[gridRow][gridCol];
    if (combos.empty()) return false;

    int num_actions = actionnode->getActions().size();
    if (brushStrategy.size() != (size_t)num_actions) return false;

    int deal_index = actionnode->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);

    std::vector<float> strategy = trainable->getAverageStrategy();
    std::vector<bool> mask = trainable->getLockedMask();

    auto it = qSolverJob->locked_strategies.find(key.toStdString());
    if (it != qSolverJob->locked_strategies.end()) {
        strategy = it->second.strategy;
        mask = it->second.mask;
    }

    for (const auto& combo : combos) {
        int private_id = -1;
        for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
            const auto& pc = (*actionnode->player_privates)[k];
            if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
                private_id = k;
                break;
            }
        }
        if (private_id < 0 || private_id >= (int)mask.size() || !mask[private_id]) {
            return false;
        }
        for (int a = 0; a < num_actions; ++a) {
            float diff = std::abs(strategy[private_id * num_actions + a] - brushStrategy[a]);
            if (diff > 0.01f) {
                return false;
            }
        }
    }
    return true;
}

bool StrategyExplorer::isComboLockedWithStrategy(int comboRow, int comboCol, const std::vector<float>& brushStrategy) {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return false;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !trainable->isLocked() || !actionnode->player_privates) return false;
    if (selectedGridRow < 0 || selectedGridCol < 0) return false;

    const auto& combos = tableStrategyModel->ui_strategy_table[selectedGridRow][selectedGridCol];
    int combo_idx = comboRow * 4 + comboCol;
    if (combo_idx < 0 || combo_idx >= (int)combos.size()) return false;

    int num_actions = actionnode->getActions().size();
    if (brushStrategy.size() != (size_t)num_actions) return false;

    int deal_index = actionnode->getTrainableIndex(trainable);
    QString nodeId = QString::fromStdString(selectedNode->getNodeId());
    QString key = QString("%1#%2").arg(nodeId).arg(deal_index);

    std::vector<float> strategy = trainable->getAverageStrategy();
    std::vector<bool> mask = trainable->getLockedMask();

    auto it = qSolverJob->locked_strategies.find(key.toStdString());
    if (it != qSolverJob->locked_strategies.end()) {
        strategy = it->second.strategy;
        mask = it->second.mask;
    }

    auto combo = combos[combo_idx];
    for (size_t k = 0; k < actionnode->player_privates->size(); ++k) {
        const auto& pc = (*actionnode->player_privates)[k];
        if ((pc.card1 == combo.first && pc.card2 == combo.second) || (pc.card1 == combo.second && pc.card2 == combo.first)) {
            if (k >= mask.size() || !mask[k]) return false;
            for (int a = 0; a < num_actions; ++a) {
                float diff = std::abs(strategy[k * num_actions + a] - brushStrategy[a]);
                if (diff > 0.01f) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

// ==================== Expression Category Selector ====================

std::vector<int> StrategyExplorer::getCurrentBoardCardInts() {
    std::vector<int> boardInts;
    // Parse the flop from the board string
    std::vector<std::string> board_str_arr = string_split(this->qSolverJob->board, ',');
    for (const auto& s : board_str_arr) {
        boardInts.push_back(Card::strCard2int(s));
    }
    
    if (selectedNode) {
        GameTreeNode::GameRound round = selectedNode->getRound();
        // Add turn card if present and round is TURN or later
        if ((round == GameTreeNode::GameRound::TURN || round == GameTreeNode::GameRound::RIVER) && 
            !this->tableStrategyModel->getTrunCard().empty()) {
            boardInts.push_back(this->tableStrategyModel->getTrunCard().getCardInt());
        }
        // Add river card if present and round is RIVER
        if (round == GameTreeNode::GameRound::RIVER && 
            !this->tableStrategyModel->getRiverCard().empty()) {
            boardInts.push_back(this->tableStrategyModel->getRiverCard().getCardInt());
        }
    }
    return boardInts;
}

bool StrategyExplorer::comboMatchesCategory(int card1, int card2, const std::vector<int>& boardCardInts, const QString& category) {
    // Extract ranks and suits from card ints
    // card_int = (rank - 2) * 4 + suit, so rank = card_int / 4 + 2, suit = card_int % 4
    int rank1 = card1 / 4 + 2;  // 2-14 (2..A)
    int suit1 = card1 % 4;       // 0-3 (c,d,h,s)
    int rank2 = card2 / 4 + 2;
    int suit2 = card2 % 4;

    // Board ranks and suits
    std::vector<int> boardRanks, boardSuits;
    for (int bc : boardCardInts) {
        boardRanks.push_back(bc / 4 + 2);
        boardSuits.push_back(bc % 4);
    }

    // All cards combined (board + hole cards)
    std::vector<int> allRanks = boardRanks;
    allRanks.push_back(rank1);
    allRanks.push_back(rank2);
    std::vector<int> allSuits = boardSuits;
    allSuits.push_back(suit1);
    allSuits.push_back(suit2);

    // Sort board ranks descending to find top/second/third pair
    std::vector<int> sortedBoardRanks = boardRanks;
    std::sort(sortedBoardRanks.begin(), sortedBoardRanks.end(), std::greater<int>());
    // Unique board ranks (sorted descending)
    std::vector<int> uniqueBoardRanks;
    for (int r : sortedBoardRanks) {
        if (uniqueBoardRanks.empty() || uniqueBoardRanks.back() != r) {
            uniqueBoardRanks.push_back(r);
        }
    }

    // Helper: does the hand pair with a board card of given rank?
    auto pairsWithRank = [&](int targetRank) -> bool {
        bool boardHasRank = false;
        for (int r : boardRanks) {
            if (r == targetRank) { boardHasRank = true; break; }
        }
        if (!boardHasRank) return false;
        return (rank1 == targetRank || rank2 == targetRank);
    };

    // Helper: count straight outs
    auto countStraightOuts = [&]() -> int {
        std::set<int> haveRanks(allRanks.begin(), allRanks.end());
        std::set<int> usedCards;
        for (int bc : boardCardInts) usedCards.insert(bc);
        usedCards.insert(card1);
        usedCards.insert(card2);

        int outs = 0;
        for (int tryRank = 2; tryRank <= 14; tryRank++) {
            if (haveRanks.count(tryRank)) continue;

            std::set<int> tempRanks = haveRanks;
            tempRanks.insert(tryRank);

            bool hasStraight = false;
            // Check wheel A-2-3-4-5
            if (tempRanks.count(14) && tempRanks.count(2) && tempRanks.count(3) &&
                tempRanks.count(4) && tempRanks.count(5)) {
                hasStraight = true;
            }
            if (!hasStraight) {
                for (int low = 2; low <= 10; low++) {
                    bool allPresent = true;
                    for (int k = 0; k < 5; k++) {
                        if (!tempRanks.count(low + k)) { allPresent = false; break; }
                    }
                    if (allPresent) { hasStraight = true; break; }
                }
            }

            if (hasStraight) {
                for (int s = 0; s < 4; s++) {
                    int candidateCard = (tryRank - 2) * 4 + s;
                    if (usedCards.count(candidateCard) == 0) {
                        outs++;
                    }
                }
            }
        }
        return outs;
    };

    // Helper: check if we already have a made straight
    auto hasMadeStraight = [&]() -> bool {
        std::set<int> haveRanks(allRanks.begin(), allRanks.end());
        if (haveRanks.count(14) && haveRanks.count(2) && haveRanks.count(3) &&
            haveRanks.count(4) && haveRanks.count(5)) return true;
        for (int low = 2; low <= 10; low++) {
            bool allPresent = true;
            for (int k = 0; k < 5; k++) {
                if (!haveRanks.count(low + k)) { allPresent = false; break; }
            }
            if (allPresent) return true;
        }
        return false;
    };

    // Helper: check if we already have a flush
    auto hasMadeFlush = [&]() -> bool {
        int suitCount[4] = {0, 0, 0, 0};
        for (int s : allSuits) suitCount[s]++;
        return suitCount[0] >= 5 || suitCount[1] >= 5 || suitCount[2] >= 5 || suitCount[3] >= 5;
    };

    // Helper: check flush draw (4 cards to a flush, at least one hole card contributing)
    auto hasFlushDraw = [&]() -> bool {
        if (hasMadeFlush()) return false;
        int suitCount[4] = {0, 0, 0, 0};
        for (int s : allSuits) suitCount[s]++;
        for (int s = 0; s < 4; s++) {
            if (suitCount[s] == 4 && (suit1 == s || suit2 == s)) return true;
        }
        return false;
    };

    // === Calculate precise hand strength ===
    std::map<int, int> rankCounts;
    for (int r : allRanks) rankCounts[r]++;

    int maxCount = 0;
    int pairCount = 0;
    int tripsCount = 0;
    int quadsCount = 0;
    for (auto const& [rank, count] : rankCounts) {
        if (count > maxCount) maxCount = count;
        if (count == 2) pairCount++;
        else if (count == 3) tripsCount++;
        else if (count == 4) quadsCount++;
    }

    bool hasMadeFlushHand = hasMadeFlush();
    bool hasMadeStraightHand = hasMadeStraight();
    
    bool isQuads = (quadsCount > 0);
    bool isFullHouse = !isQuads && ((tripsCount >= 2) || (tripsCount == 1 && pairCount >= 1));
    bool isFlush = !isQuads && !isFullHouse && hasMadeFlushHand;
    bool isStraight = !isQuads && !isFullHouse && !isFlush && hasMadeStraightHand;
    bool isTripsOrSet = !isQuads && !isFullHouse && !isFlush && !isStraight && (tripsCount >= 1);
    
    bool isStrongHand = isQuads || isFullHouse || isFlush || isStraight || isTripsOrSet;

    bool matchesTopPair = false;
    bool matchesSecondPair = false;
    bool matchesThirdPair = false;
    bool matchesOverpair = false;
    bool matchesPPBelowTop = false;
    bool matchesPPBelowSecond = false;
    bool matchesUnderpair = false;
    bool matchesTwoPair = false;

    if (!isStrongHand) {
        if (rank1 != rank2) {
            bool pair1 = false, pair2 = false;
            for (int br : boardRanks) {
                if (br == rank1) pair1 = true;
                if (br == rank2) pair2 = true;
            }
            if (pair1 && pair2) {
                matchesTwoPair = true;
            } else {
                if (uniqueBoardRanks.size() >= 1 && pairsWithRank(uniqueBoardRanks[0])) matchesTopPair = true;
                else if (uniqueBoardRanks.size() >= 2 && pairsWithRank(uniqueBoardRanks[1])) matchesSecondPair = true;
                else if (uniqueBoardRanks.size() >= 3 && pairsWithRank(uniqueBoardRanks[2])) matchesThirdPair = true;
            }
        } else {
            if (uniqueBoardRanks.size() >= 1 && rank1 > uniqueBoardRanks[0]) {
                matchesOverpair = true;
            } else if (uniqueBoardRanks.size() >= 2 && rank1 > uniqueBoardRanks[1]) {
                matchesPPBelowTop = true;
            } else if (uniqueBoardRanks.size() >= 3 && rank1 > uniqueBoardRanks[2]) {
                matchesPPBelowSecond = true;
            } else {
                matchesUnderpair = true;
            }
        }
    }

    bool matchesAceHigh = false;
    bool matchesKingHigh = false;
    if (!isStrongHand && !matchesTwoPair && !matchesTopPair && !matchesSecondPair && !matchesThirdPair && !matchesOverpair && rank1 != rank2) {
        bool pairsBoard = false;
        for (int br : boardRanks) {
            if (br == rank1 || br == rank2) pairsBoard = true;
        }
        if (!pairsBoard) {
            int highCard = std::max(rank1, rank2);
            if (highCard == 14) matchesAceHigh = true;
            else if (highCard == 13) matchesKingHigh = true;
        }
    }

    // === Category matching ===

    if (category == "Quads") return isQuads;
    if (category == "Full House") return isFullHouse;
    if (category == "Flush") return isFlush;
    if (category == "Straight") return isStraight;
    if (category == "Trips/Set") return isTripsOrSet;

    if (category == "Top Pair") return matchesTopPair;
    if (category == "Second Pair") return matchesSecondPair;
    if (category == "Third Pair") return matchesThirdPair;
    if (category == "Overpair") return matchesOverpair;
    if (category == "PP < Top Card") return matchesPPBelowTop;
    if (category == "PP < 2nd Card") return matchesPPBelowSecond;
    if (category == "Underpair") return matchesUnderpair;
    if (category == "Two Pair") return matchesTwoPair;

    if (category == "Flush Draw") {
        return hasFlushDraw();
    }
    if (category == "Combo Draw") {
        return hasFlushDraw() && (countStraightOuts() >= 4);
    }
    if (category == "Straight Draw (8+ outs)") {
        if (hasMadeStraightHand || hasMadeFlushHand) return false;
        return countStraightOuts() >= 8;
    }
    if (category == "Straight Draw (4 outs)") {
        if (hasMadeStraightHand || hasMadeFlushHand) return false;
        int outs = countStraightOuts();
        return outs >= 4 && outs < 8;
    }
    if (category == "Ace High") return matchesAceHigh;
    if (category == "King High") return matchesKingHigh;

    return false;
}

void StrategyExplorer::updateComboHighlightsForCell(int gridRow, int gridCol) {
    memset(detailWindowSetting.comboHighlight, 0, sizeof(detailWindowSetting.comboHighlight));

    if (!detailWindowSetting.hasExpressionHighlight) return;
    if (gridRow < 0 || gridCol < 0) return;
    if (gridRow >= 13 || gridCol >= 13) return;
    if (tableStrategyModel->ui_strategy_table.empty()) return;
    if (gridRow >= (int)tableStrategyModel->ui_strategy_table.size()) return;
    if (gridCol >= (int)tableStrategyModel->ui_strategy_table[gridRow].size()) return;

    const auto& combos = tableStrategyModel->ui_strategy_table[gridRow][gridCol];
    std::vector<int> boardInts = getCurrentBoardCardInts();

    for (int idx = 0; idx < (int)combos.size() && idx < 16; idx++) {
        int c1 = combos[idx].first;
        int c2 = combos[idx].second;
        if (comboMatchesCategory(c1, c2, boardInts, detailWindowSetting.selectedCategory)) {
            detailWindowSetting.comboHighlight[idx] = true;
        }
    }
}

void StrategyExplorer::updateSlidersForCategory(const QString& category) {
    if (!selectedNode || selectedNode->getType() != GameTreeNode::GameTreeNodeType::ACTION) return;
    auto actionnode = static_pointer_cast<ActionNode>(selectedNode);
    auto trainable = this->tableStrategyModel->get_current_trainable();
    if (!trainable || !actionnode->player_privates) return;

    int num_actions = actionnode->getActions().size();
    std::vector<int> boardInts = getCurrentBoardCardInts();

    std::vector<float> avgStrategy(num_actions, 0.0f);
    int matchCount = 0;

    for (int r = 0; r < 13 && r < (int)tableStrategyModel->ui_strategy_table.size(); r++) {
        for (int c = 0; c < 13 && c < (int)tableStrategyModel->ui_strategy_table[r].size(); c++) {
            const auto& combos = tableStrategyModel->ui_strategy_table[r][c];
            for (const auto& combo : combos) {
                int c1 = combo.first;
                int c2 = combo.second;
                if (comboMatchesCategory(c1, c2, boardInts, category)) {
                    if (c1 < (int)tableStrategyModel->current_strategy.size() &&
                        c2 < (int)tableStrategyModel->current_strategy[c1].size()) {
                        const auto& strat = tableStrategyModel->current_strategy[c1][c2];
                        for (int a = 0; a < num_actions && a < (int)strat.size(); a++) {
                            avgStrategy[a] += strat[a];
                        }
                        matchCount++;
                    }
                }
            }
        }
    }

    if (matchCount > 0) {
        for (int a = 0; a < num_actions; a++) {
            avgStrategy[a] /= matchCount;
        }
    }

    updateSliderValues(avgStrategy);
}

void StrategyExplorer::on_expressionSelectBox_currentIndexChanged(int index) {
    if (index <= 0) {
        // [None] selected - clear all highlights
        detailWindowSetting.hasExpressionHighlight = false;
        detailWindowSetting.selectedCategory.clear();
        memset(detailWindowSetting.cellHighlight, 0, sizeof(detailWindowSetting.cellHighlight));
        memset(detailWindowSetting.comboHighlight, 0, sizeof(detailWindowSetting.comboHighlight));
    } else {
        QString category = this->ui->expressionSelectBox->currentText();
        detailWindowSetting.selectedCategory = category;
        detailWindowSetting.hasExpressionHighlight = true;

        // Compute cell highlights for all 13x13 grid cells
        memset(detailWindowSetting.cellHighlight, 0, sizeof(detailWindowSetting.cellHighlight));
        std::vector<int> boardInts = getCurrentBoardCardInts();

        for (int r = 0; r < 13 && r < (int)tableStrategyModel->ui_strategy_table.size(); r++) {
            for (int c = 0; c < 13 && c < (int)tableStrategyModel->ui_strategy_table[r].size(); c++) {
                const auto& combos = tableStrategyModel->ui_strategy_table[r][c];
                for (const auto& combo : combos) {
                    if (comboMatchesCategory(combo.first, combo.second, boardInts, category)) {
                        detailWindowSetting.cellHighlight[r][c] = true;
                        break;  // At least one combo matches, highlight the cell
                    }
                }
            }
        }

        // Update combo highlights for the currently hovered cell
        updateComboHighlightsForCell(detailWindowSetting.grid_i, detailWindowSetting.grid_j);

        // Update sliders to show GTO average for matching combos
        updateSlidersForCategory(category);
    }

    // Refresh views
    this->ui->strategyTableView->viewport()->update();
    this->ui->detailView->viewport()->update();
    
    updateLockEditor();
}
