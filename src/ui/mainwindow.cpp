#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "stdio.h"
#include "include/runtime/qsolverjob.h"
#include <QFileDialog>
#include <QTimer>
#include <QComboBox>
#include <sstream>
#include <QDesktopServices>
#include "include/tools/utils.h"
#include "include/library.h"
#include <thread>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#include "include/ui/pt4importdialog.h"
#include "include/ui/profileclusterexplorer.h"
#include "include/ui/autoevdialog.h"
#include "include/preflop/preflopsolverwindow.h"
#include "include/ui/rangeselectortabledelegate.h"
#include "include/globals.h"
#include <QSqlQueryModel>
#include <QSqlRecord>
#include "include/tools/dbmanager.h"
#include <QInputDialog>
#include <QDateTime>

QSTextEdit* MainWindow::s_textEdit = 0;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    MainWindow::s_textEdit = this->get_logwindow();

    // System hardware detection (CPU cores and RAM)
    unsigned int cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 8; // fallback default
    this->ui->threadsText->setText(QString::number(cores));

    double ram_gb = 0.0;
#ifdef Q_OS_UNIX
    long long pages = sysconf(_SC_PHYS_PAGES);
    long long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        ram_gb = (double)(pages * page_size) / (1024.0 * 1024.0 * 1024.0);
    }
#endif

    QString sysInfo = tr("System Info: Detected ") + QString::number(cores) + tr(" logical CPU cores");
    if (ram_gb > 0.0) {
        sysInfo += tr(" and ") + QString::number(ram_gb, 'f', 1) + tr(" GB of system RAM.");
    } else {
        sysInfo += tr(".");
    }
    qDebug().noquote() << sysInfo;
    connect(this->ui->actionjson, &QAction::triggered, this, &MainWindow::on_actionjson_triggered);
    connect(this->ui->actionSettings, &QAction::triggered, this, &MainWindow::on_actionSettings_triggered);
    connect(this->ui->actionimport, &QAction::triggered, this, &MainWindow::on_actionimport_triggered);
    connect(this->ui->actionexport, &QAction::triggered, this, &MainWindow::on_actionexport_triggered);
    connect(this->ui->actionclear_all, &QAction::triggered, this, &MainWindow::on_actionclear_all_triggered);

    QAction *actionExplorer = new QAction(tr("Profile Cluster Explorer"), this);
    this->ui->menusolver->addAction(actionExplorer);
    connect(actionExplorer, &QAction::triggered, this, [this](){
        ProfileClusterExplorer *explorer = new ProfileClusterExplorer(this);
        explorer->setAttribute(Qt::WA_DeleteOnClose);
        explorer->show();
    });

    qSolverJob = new QSolverJob;
    connect(qSolverJob, &QThread::finished, this, &MainWindow::onSolverJobFinished);
    qSolverJob->setContext(this->getLogArea());
    qSolverJob->current_mission = QSolverJob::MissionType::LOADING;
    qSolverJob->start();
    this->setWindowTitle(tr("TexasSolver"));

    // parameters table view
    qParametersModel = DBManager::instance().getParametersModel(this);
    if (qParametersModel) {
        this->ui->parametersTableView->setModel(this->qParametersModel);
        this->ui->parametersTableView->hideColumn(0); // Hide ID column
        this->ui->parametersTableView->resizeColumnsToContents();
    }

    // solves tree view
    qSolvesModel = DBManager::instance().getSolvesModel(this);
    if (qSolvesModel) {
        this->ui->solvesTreeView->setModel(this->qSolvesModel);
    }
    connect(
                this->ui->solvesTreeView,
                SIGNAL(clicked(const QModelIndex&)),
                this,
                SLOT(solve_item_clicked(const QModelIndex&))
                );
    this->ui->solvesTreeView->hideColumn(0); // Hide ID column
    this->ui->solvesTreeView->resizeColumnsToContents();
    // Thumbnail process
    this->ip_model = new RangeSelectorTableModel(QString("A,K,Q,J,T,9,8,7,6,5,4,3,2").split(","),this->ui->ipRangeText->toPlainText(),this,true);
    this->ip_delegate = new RangeSelectorTableDelegate(QString("A,K,Q,J,T,9,8,7,6,5,4,3,2").split(","),this->ip_model,this);
    this->ui->IpRangeTableView->setModel(this->ip_model);
    this->ui->IpRangeTableView->setItemDelegate(this->ip_delegate);
    this->ui->IpRangeTableView->verticalHeader()->setMinimumSectionSize(1);
    this->ui->IpRangeTableView->horizontalHeader()->setMinimumSectionSize(1);

    this->oop_model = new RangeSelectorTableModel(QString("A,K,Q,J,T,9,8,7,6,5,4,3,2").split(","),this->ui->oopRangeText->toPlainText(),this,true);
    this->oop_delegate = new RangeSelectorTableDelegate(QString("A,K,Q,J,T,9,8,7,6,5,4,3,2").split(","),this->oop_model,this);
    this->ui->oopRangeTableView->setModel(this->oop_model);
    this->ui->oopRangeTableView->setItemDelegate(this->oop_delegate);
    this->ui->oopRangeTableView->verticalHeader()->setMinimumSectionSize(1);
    this->ui->oopRangeTableView->horizontalHeader()->setMinimumSectionSize(1);
    this->ui->tabWidget->hide();

    int defaultSolverIndex = -1;
    int fallbackIndex = 0;
    for(int i = 0; i < (int)SolverEngineOptions.size(); i++) {
        const auto& option = SolverEngineOptions[i];
        this->ui->solverTypeComboBox->addItem(option.second, static_cast<int>(option.first));
        if (option.first == SolverEngine::CPU_PCFR) {
            fallbackIndex = i;
        }
#ifdef USE_GPU_CUDA
        if (option.first == SolverEngine::GPU_CUDA && has_nvidia_gpu()) {
            defaultSolverIndex = i;
        }
#endif
#ifdef USE_GPU_HIP
        if (defaultSolverIndex == -1 && option.first == SolverEngine::GPU_HIP && has_amd_gpu()) {
            defaultSolverIndex = i;
        }
#endif
    }
    if (defaultSolverIndex == -1) defaultSolverIndex = fallbackIndex;
    this->ui->solverTypeComboBox->setCurrentIndex(defaultSolverIndex);

    connect(this->ui->solverTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index){
        SolverEngine selectedSolver = static_cast<SolverEngine>(this->ui->solverTypeComboBox->itemData(index).toInt());
        this->ui->gpuFp16CheckBox->setVisible(selectedSolver == SolverEngine::GPU_HIP);
    });
    // Trigger it once to set initial state
    emit this->ui->solverTypeComboBox->currentIndexChanged(this->ui->solverTypeComboBox->currentIndex());

    connect(this->ui->importPT4HandButton, &QPushButton::clicked, this, [this](){
        PT4ImportDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted && dialog.hasSelectedHand()) {
            ImportedHand hand = dialog.getSelectedHand();
            
            if (dialog.isPreflopImport()) {
                if (!this->preflopSolverWindow) {
                    this->preflopSolverWindow = new PreflopSolverWindow(this->qSolverJob, this);
                    this->preflopSolverWindow->setAttribute(Qt::WA_DeleteOnClose);
                    connect(this->preflopSolverWindow, &QObject::destroyed, this, [this]() {
                        this->preflopSolverWindow = nullptr;
                    });
                }
                this->preflopSolverWindow->importPreflopHand(hand);
                this->preflopSolverWindow->show();
                this->preflopSolverWindow->raise();
                this->preflopSolverWindow->activateWindow();
                QTimer::singleShot(100, this->preflopSolverWindow, &QWidget::showMaximized);
            } else {
                // Set board
                ui->boardText->setText(hand.board);
                // Set pot
                ui->potText->setText(QString::number(hand.pot));
                // Set effective stack
                ui->effectiveStackText->setText(QString::number(hand.effective_stack));
                // Set ranges
                ui->ipRangeText->setText(hand.ip_range);
                ui->oopRangeText->setText(hand.oop_range);
                
                // Set bet/raise sizes from profile
                ui->flop_ip_bet->setText(hand.ip_flop_bet_sizes);
                ui->turn_ip_bet->setText(hand.ip_turn_bet_sizes);
                ui->river_ip_bet->setText(hand.ip_river_bet_sizes);

                ui->flop_oop_bet->setText(hand.oop_flop_bet_sizes);
                ui->turn_oop_bet->setText(hand.oop_turn_bet_sizes);
                ui->river_oop_bet->setText(hand.oop_river_bet_sizes);

                // Raise sizes
                ui->flop_ip_raise->setText(hand.ip_flop_raise_sizes);
                ui->turn_ip_raise->setText(hand.ip_turn_raise_sizes);
                ui->river_ip_raise->setText(hand.ip_river_raise_sizes);

                ui->flop_oop_raise->setText(hand.oop_flop_raise_sizes);
                ui->turn_oop_raise->setText(hand.oop_turn_raise_sizes);
                ui->river_oop_raise->setText(hand.oop_river_raise_sizes);

                // Donk sizes from profile
                ui->turn_oop_donk->setText(hand.oop_turn_donk_sizes);
                ui->river_oop_donk->setText(hand.oop_river_donk_sizes);
                
                this->m_currentHandId = hand.id_hand;
                this->m_currentParentSolveId = -1; // Reset lineage for new imported hand
                ui->iterationText->setText("5000");
                ui->logIntervalText->setText("80");

                // All-in options
                ui->flop_ip_allin->setChecked(hand.ip_flop_allin);
                ui->turn_ip_allin->setChecked(hand.ip_turn_allin);
                ui->river_ip_allin->setChecked(hand.ip_river_allin);

                ui->flop_oop_allin->setChecked(hand.oop_flop_allin);
                ui->turn_oop_allin->setChecked(hand.oop_turn_allin);
                ui->river_oop_allin->setChecked(hand.oop_river_allin);

                // Save turn and river cards for later use in StrategyExplorer
                ui->preselectedTurnEdit->setText(hand.turn_card);
                ui->preselectedRiverEdit->setText(hand.river_card);
            }
        }
    });

    connect(this->ui->autoEvButton, &QPushButton::clicked, this, [this](){
        AutoEvDialog dialog(this);
        dialog.exec();
    });

    // Maximize the window on start after the event loop maps the window
    QTimer::singleShot(100, this, [this]() {
        this->setWindowState(this->windowState() | Qt::WindowMaximized);
    });
}

QSTextEdit * MainWindow::get_logwindow(){
    return this->ui->logOutput;
}

MainWindow::~MainWindow()
{
    if (this->strategyExplorer) {
        delete this->strategyExplorer;
    }
    delete qSolverJob;
    delete qParametersModel;
    delete qSolvesModel;
    delete ip_delegate;
    delete ip_model;
    delete oop_delegate;
    delete oop_model;
    delete ui;
}

void MainWindow::on_actionjson_triggered(){
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                               "output_strategy.json",
                               tr("Json file (*.json)"));
    if(fileName.isNull())return;
    this->qSolverJob->savefile = fileName;
    qSolverJob->current_mission = QSolverJob::MissionType::SAVING;
    qSolverJob->start();
}

QString getParams(QString input,QString key){
    if(input.contains(key)){
        return input.replace(key,"").trimmed();
    }else{
        return "INVALID";
    }
}

void MainWindow::on_actionclear_all_triggered(){
    if (this->strategyExplorer) {
        delete this->strategyExplorer;
    }
    this->clear_all_params();
    this->ui->IpRangeTableView->update();
    this->ui->oopRangeTableView->update();
    this->ui->IpRangeTableView->setFocus();
    this->ui->oopRangeTableView->setFocus();
}

void MainWindow::clear_all_params(){
    this->ui->potText->clear();
    this->ui->effectiveStackText->clear();
    this->ui->boardText->clear();
    this->ui->oopRangeText->clear();
    this->ui->ipRangeText->clear();
    this->ui->flop_oop_bet->clear();
    this->ui->flop_oop_raise->clear();
    this->ui->flop_oop_allin->setChecked(false);
    this->ui->flop_ip_bet->clear();
    this->ui->flop_ip_raise->clear();
    this->ui->flop_ip_allin->setChecked(false);
    this->ui->turn_oop_bet->clear();
    this->ui->turn_oop_raise->clear();
    this->ui->turn_oop_donk->clear();
    this->ui->turn_oop_allin->setChecked(false);
    this->ui->turn_ip_bet->clear();
    this->ui->turn_ip_raise->clear();
    this->ui->turn_ip_allin->setChecked(false);
    this->ui->river_oop_bet->clear();
    this->ui->river_oop_raise->clear();
    this->ui->river_oop_donk->clear();
    this->ui->river_oop_allin->setChecked(false);
    this->ui->river_ip_bet->clear();
    this->ui->river_ip_raise->clear();
    this->ui->river_ip_allin->setChecked(false);
    this->ui->allinThresholdText->clear();
    unsigned int cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 8;
    this->ui->threadsText->setText(QString::number(cores));
    this->ui->exploitabilityText->clear();
    this->ui->iterationText->clear();
    this->ui->logIntervalText->clear();
    this->ui->raiseLimitText->clear();
    this->ui->useIsoCheck->setChecked(false);
}

void MainWindow::import_from_file(QString fileName){
    if( fileName.isNull() )
    {
        qDebug().noquote() << tr("File selection invalid.");
        return;
    }
    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly)){
        qDebug().noquote() << tr("File open failed.");
        return;
    }
    QString content;
    QTextStream s1(&file);
    content.append(s1.readAll());
    this->import_from_content(content);
}

void MainWindow::import_from_content(QString content){
    this->clear_all_params();
    QStringList raw_lines = content.split("\n");
    QStringList cleaned_lines;
    for (const QString& line : raw_lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        
        bool is_command = false;
        static const QStringList commands = {
            "set_pot", "set_effective_stack", "set_board", "set_preselected_cards",
            "set_range_oop", "set_range_ip", "set_bet_sizes", "set_allin_threshold",
            "set_raise_limit", "build_tree", "set_thread_num", "set_accuracy",
            "set_max_iteration", "set_print_interval", "set_use_isomorphism",
            "start_solve", "set_dump_rounds", "dump_result"
        };
        for (const QString& cmd : commands) {
            if (trimmed.startsWith(cmd)) {
                is_command = true;
                break;
            }
        }
        
        if (is_command) {
            cleaned_lines.append(trimmed);
        } else {
            if (!cleaned_lines.isEmpty()) {
                cleaned_lines.last() += trimmed;
            } else {
                cleaned_lines.append(trimmed);
            }
        }
    }

    for(QString one_line_content:cleaned_lines){
        std::cout << "CLEANED CONFIG LINE: " << one_line_content.toStdString() << std::endl;
        if(getParams(one_line_content,"set_pot") != "INVALID"){
            this->ui->potText->setText(getParams(one_line_content,"set_pot"));
        }
        else if(getParams(one_line_content,"set_effective_stack") != "INVALID"){
            this->ui->effectiveStackText->setText(getParams(one_line_content,"set_effective_stack"));
        }
        else if(getParams(one_line_content,"set_board") != "INVALID"){
            this->ui->boardText->setText(getParams(one_line_content,"set_board"));
        }
        else if(getParams(one_line_content,"set_range_oop") != "INVALID"){
            this->ui->oopRangeText->setText(getParams(one_line_content,"set_range_oop"));
        }
        else if(getParams(one_line_content,"set_range_ip") != "INVALID"){
            this->ui->ipRangeText->setText(getParams(one_line_content,"set_range_ip"));
        }
        // FLOP
        else if(getParams(one_line_content,"set_bet_sizes oop,flop,bet,") != "INVALID"){
            this->ui->flop_oop_bet->setText(getParams(one_line_content,"set_bet_sizes oop,flop,bet,").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,flop,raise") != "INVALID"){
            this->ui->flop_oop_raise->setText(getParams(one_line_content,"set_bet_sizes oop,flop,raise").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,flop,allin") != "INVALID"){
            this->ui->flop_oop_allin->setChecked(true);
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,flop,bet,") != "INVALID"){
            this->ui->flop_ip_bet->setText(getParams(one_line_content,"set_bet_sizes ip,flop,bet,").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,flop,raise") != "INVALID"){
            this->ui->flop_ip_raise->setText(getParams(one_line_content,"set_bet_sizes ip,flop,raise").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,flop,allin") != "INVALID"){
            this->ui->flop_ip_allin->setChecked(true);
        }
        // TURN
        else if(getParams(one_line_content,"set_bet_sizes oop,turn,bet,") != "INVALID"){
            this->ui->turn_oop_bet->setText(getParams(one_line_content,"set_bet_sizes oop,turn,bet,").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,turn,raise") != "INVALID"){
            this->ui->turn_oop_raise->setText(getParams(one_line_content,"set_bet_sizes oop,turn,raise").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,turn,donk") != "INVALID"){
            this->ui->turn_oop_donk->setText(getParams(one_line_content,"set_bet_sizes oop,turn,donk").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,turn,allin") != "INVALID"){
            this->ui->turn_oop_allin->setChecked(true);
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,turn,bet,") != "INVALID"){
            this->ui->turn_ip_bet->setText(getParams(one_line_content,"set_bet_sizes ip,turn,bet,").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,turn,raise") != "INVALID"){
            this->ui->turn_ip_raise->setText(getParams(one_line_content,"set_bet_sizes ip,turn,raise").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,turn,allin") != "INVALID"){
            this->ui->turn_ip_allin->setChecked(true);
        }
        // RIVER
        else if(getParams(one_line_content,"set_bet_sizes oop,river,bet,") != "INVALID"){
            this->ui->river_oop_bet->setText(getParams(one_line_content,"set_bet_sizes oop,river,bet,").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,river,raise") != "INVALID"){
            this->ui->river_oop_raise->setText(getParams(one_line_content,"set_bet_sizes oop,river,raise").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,river,donk") != "INVALID"){
            this->ui->river_oop_donk->setText(getParams(one_line_content,"set_bet_sizes oop,river,donk").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes oop,river,allin") != "INVALID"){
            this->ui->river_oop_allin->setChecked(true);
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,river,bet,") != "INVALID"){
            this->ui->river_ip_bet->setText(getParams(one_line_content,"set_bet_sizes ip,river,bet,").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,river,raise") != "INVALID"){
            this->ui->river_ip_raise->setText(getParams(one_line_content,"set_bet_sizes ip,river,raise").replace(',',' '));
        }
        else if(getParams(one_line_content,"set_bet_sizes ip,river,allin") != "INVALID"){
            this->ui->river_ip_allin->setChecked(true);
        }
        // OTHER PARAMS
        else if(getParams(one_line_content,"set_allin_threshold") != "INVALID"){
            this->ui->allinThresholdText->setText(getParams(one_line_content,"set_allin_threshold"));
        }
        else if(getParams(one_line_content,"set_thread_num") != "INVALID"){
            this->ui->threadsText->setText(getParams(one_line_content,"set_thread_num"));
        }
        else if(getParams(one_line_content,"set_accuracy") != "INVALID"){
            this->ui->exploitabilityText->setText(getParams(one_line_content,"set_accuracy"));
        }
        else if(getParams(one_line_content,"set_max_iteration") != "INVALID"){
            this->ui->iterationText->setText(getParams(one_line_content,"set_max_iteration"));
        }
        else if(getParams(one_line_content,"set_print_interval") != "INVALID"){
            this->ui->logIntervalText->setText(getParams(one_line_content,"set_print_interval"));
        }
        else if(getParams(one_line_content,"set_raise_limit") != "INVALID"){
            this->ui->raiseLimitText->setText(getParams(one_line_content,"set_raise_limit"));
        }
        else if(getParams(one_line_content,"set_use_isomorphism") != "INVALID"){
            if(getParams(one_line_content,"set_use_isomorphism") == "1"){
                this->ui->useIsoCheck->setChecked(true);
            }else{
                this->ui->useIsoCheck->setChecked(false);
            }
        }
        else if(getParams(one_line_content,"set_preselected_cards") != "INVALID"){
            QString preselected_cards = getParams(one_line_content,"set_preselected_cards");
            QStringList cards = preselected_cards.split(",");
            if(cards.size() > 0) this->ui->preselectedTurnEdit->setText(cards[0]);
            if(cards.size() > 1) this->ui->preselectedRiverEdit->setText(cards[1]);
        }
    }
    this->update();
    std::cout << "DEBUG IMPORT: potText=" << this->ui->potText->text().toStdString()
              << ", effectiveStackText=" << this->ui->effectiveStackText->text().toStdString()
              << ", boardText=" << this->ui->boardText->toPlainText().toStdString()
              << ", oopRange size=" << this->ui->oopRangeText->toPlainText().size()
              << ", ipRange size=" << this->ui->ipRangeText->toPlainText().size() << std::endl;
}

void MainWindow::on_actionimport_triggered(){
    QString fileName =  QFileDialog::getOpenFileName(
              this,
              tr("Open parameters file"),
              QDir::currentPath(),
              tr("Text files (*.txt)"));
    this->import_from_file(fileName);
    this->ui->IpRangeTableView->update();
    this->ui->oopRangeTableView->update();
    this->ui->IpRangeTableView->setFocus();
    this->ui->oopRangeTableView->setFocus();
}
QString MainWindow::export_to_string(){
    QString output_text = "";
    QTextStream out(&output_text);
    out << "set_pot " << this->ui->potText->text().trimmed();
    out << "\n";
    out << "set_effective_stack " << this->ui->effectiveStackText->text().trimmed();
    out << "\n";
    out << "set_board " << this->ui->boardText->toPlainText();
    out << "\n";
    if(!this->ui->preselectedTurnEdit->text().trimmed().isEmpty() || !this->ui->preselectedRiverEdit->text().trimmed().isEmpty()){
        out << "set_preselected_cards ";
        if(!this->ui->preselectedTurnEdit->text().trimmed().isEmpty()){
            out << this->ui->preselectedTurnEdit->text().trimmed();
        }
        if(!this->ui->preselectedRiverEdit->text().trimmed().isEmpty()){
            out << "," << this->ui->preselectedRiverEdit->text().trimmed();
        }
        out << "\n";
    }
    out << "set_range_oop " << this->ui->oopRangeText->toPlainText().replace("\n", "").replace("\r", "").trimmed();
    out << "\n";
    out << "set_range_ip " << this->ui->ipRangeText->toPlainText().replace("\n", "").replace("\r", "").trimmed();
    out << "\n";
    // FLOP
    if(!this->ui->flop_oop_bet->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,flop,bet," << this->ui->flop_oop_bet->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->flop_oop_raise->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,flop,raise," << this->ui->flop_oop_raise->text().trimmed().replace(' ',',') << "\n";
    }
    if(this->ui->flop_oop_allin->isChecked()){
        out << "set_bet_sizes oop,flop,allin\n";
    }
    if(!this->ui->flop_ip_bet->text().trimmed().isEmpty()){
        out << "set_bet_sizes ip,flop,bet," << this->ui->flop_ip_bet->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->flop_ip_raise->text().trimmed().isEmpty()){
        out << "set_bet_sizes ip,flop,raise," << this->ui->flop_ip_raise->text().trimmed().replace(' ',',') << "\n";
    }
    if(this->ui->flop_ip_allin->isChecked()){
        out << "set_bet_sizes ip,flop,allin\n";
    }
    // TURN
    if(!this->ui->turn_oop_bet->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,turn,bet," << this->ui->turn_oop_bet->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->turn_oop_raise->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,turn,raise," << this->ui->turn_oop_raise->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->turn_oop_donk->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,turn,donk," << this->ui->turn_oop_donk->text().trimmed().replace(' ',',') << "\n";
    }
    if(this->ui->turn_oop_allin->isChecked()){
        out << "set_bet_sizes oop,turn,allin\n";
    }
    if(!this->ui->turn_ip_bet->text().trimmed().isEmpty()){
        out << "set_bet_sizes ip,turn,bet," << this->ui->turn_ip_bet->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->turn_ip_raise->text().trimmed().isEmpty()){
        out << "set_bet_sizes ip,turn,raise," << this->ui->turn_ip_raise->text().trimmed().replace(' ',',') << "\n";
    }
    if(this->ui->turn_ip_allin->isChecked()){
        out << "set_bet_sizes ip,turn,allin\n";
    }
    // RIVER
    if(!this->ui->river_oop_bet->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,river,bet," << this->ui->river_oop_bet->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->river_oop_raise->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,river,raise," << this->ui->river_oop_raise->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->river_oop_donk->text().trimmed().isEmpty()){
        out << "set_bet_sizes oop,river,donk," << this->ui->river_oop_donk->text().trimmed().replace(' ',',') << "\n";
    }
    if(this->ui->river_oop_allin->isChecked()){
        out << "set_bet_sizes oop,river,allin\n";
    }
    if(!this->ui->river_ip_bet->text().trimmed().isEmpty()){
        out << "set_bet_sizes ip,river,bet," << this->ui->river_ip_bet->text().trimmed().replace(' ',',') << "\n";
    }
    if(!this->ui->river_ip_raise->text().trimmed().isEmpty()){
        out << "set_bet_sizes ip,river,raise," << this->ui->river_ip_raise->text().trimmed().replace(' ',',') << "\n";
    }
    if(this->ui->river_ip_allin->isChecked()){
        out << "set_bet_sizes ip,river,allin\n";
    }

    out << "set_allin_threshold " << this->ui->allinThresholdText->text().trimmed();
    out << "\n";
    out << "set_raise_limit " << this->ui->raiseLimitText->text().trimmed();
    out << "\n";
    out << "build_tree";
    out << "\n";
    out << "set_thread_num " << this->ui->threadsText->text().trimmed();
    out << "\n";
    out << "set_accuracy " << this->ui->exploitabilityText->text().trimmed();
    out << "\n";
    out << "set_max_iteration " << this->ui->iterationText->text().trimmed();
    out << "\n";
    out << "set_print_interval " << this->ui->logIntervalText->text().trimmed();
    out << "\n";
    if(this->ui->useIsoCheck->isChecked()){
        out << "set_use_isomorphism 1" << "\n";
    }else{
        out << "set_use_isomorphism 0" << "\n";
    }
    out << "start_solve";
    out << "\n";

    QSettings setting("TexasSolver", "Setting");
    setting.beginGroup("solver");
    int dump_round = setting.value("dump_round").toInt();
    out << "set_dump_rounds " << dump_round;
    out << "\n";
    out << "dump_result output_result.json\n";

    return output_text;
}

void MainWindow::on_actionexport_triggered(){
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Parameters"),
                               "parameters/output_parameters.txt",
                               tr("Text file (*.txt)"));
    if(fileName.isNull())return;

    QString output_text = this->export_to_string();

    setlocale(LC_ALL,"");
    ofstream fileWriter;
    fileWriter.open(fileName.toLocal8Bit());
    QMessageBox msgBox;
    QString message;
    if(!fileWriter.fail()){
        fileWriter << output_text.toStdString();
        fileWriter.flush();
        fileWriter.close();
        message = QObject::tr("save success");
    }else{
        message = QObject::tr("save failed, file cannot be open");
    }
    qDebug().noquote() << message;
    msgBox.setText(message);
    setlocale(LC_CTYPE, "C");
    msgBox.exec();
}

void MainWindow::on_actionSettings_triggered(){
    this->settingEditor = new SettingEditor(this);
    settingEditor->setAttribute(Qt::WA_DeleteOnClose);
    settingEditor->show();
}

void MainWindow::on_ip_range(QString range_text){
    this->ui->ipRangeText->setText(range_text);
}

void MainWindow::on_buttomSolve_clicked()
{   
    int threads = ui->threadsText->text().toInt();
    unsigned int max_cores = std::thread::hardware_concurrency();
    if (max_cores > 0 && threads > (int)max_cores) {
        qDebug().noquote() << tr("WARNING: Running with more threads (") + QString::number(threads) + tr(") than logical CPU cores (") + QString::number(max_cores) + tr("). This may degrade performance.");
    }

    int selectedSolver = this->ui->solverTypeComboBox->currentData().toInt();
    qSolverJob->engineType = static_cast<SolverEngine>(selectedSolver);
    qSolverJob->max_iteration = ui->iterationText->text().toInt();
    qSolverJob->accuracy = ui->exploitabilityText->text().toFloat();
    qSolverJob->print_interval = ui->logIntervalText->text().toInt();
    qSolverJob->thread_number = threads;
    qSolverJob->current_mission = QSolverJob::MissionType::SOLVING;
    qSolverJob->start();

    // Automatically open strategy explorer maximized when solving starts
    openStrategyExplorer(true);
}

#include <zlib.h>
#include <QJsonDocument>
#include <QJsonObject>
#include "include/ui/handhistoryparser.h"

Ui::MainWindow * MainWindow::getPriUi(){
    return this->ui;
}

QSTextEdit * MainWindow::getLogArea(){
    return this->ui->logOutput;
}

void MainWindow::on_clearLogButtom_clicked()
{
    this->ui->logOutput->clear();
}

vector<float> sizes_convert(QString input){
    QStringList list = input.split(" ");
    vector<float> sizes;
    foreach(QString num, list){
        QString trimmedNum = num.trimmed().toLower();
        if(trimmedNum.isEmpty()) continue;
        if(trimmedNum.endsWith("x")){
            int pos = trimmedNum.lastIndexOf(QChar('x'));
            trimmedNum = trimmedNum.left(pos);
            sizes.push_back(trimmedNum.toFloat()  * 100);
        }
        else if(trimmedNum.endsWith("c") || trimmedNum.endsWith("f")){
            int pos = trimmedNum.endsWith("c") ? trimmedNum.lastIndexOf(QChar('c')) : trimmedNum.lastIndexOf(QChar('f'));
            trimmedNum = trimmedNum.left(pos);
            sizes.push_back(-trimmedNum.toFloat());
        }
        else{
            sizes.push_back(trimmedNum.toFloat());
        }
    }
    return sizes;
}


void MainWindow::on_buildTreeButtom_clicked()
{
    if (this->strategyExplorer) {
        delete this->strategyExplorer;
    }
    qSolverJob->clearAllLocks();
    qSolverJob->range_ip = this->ui->ipRangeText->toPlainText().toStdString();
    qSolverJob->range_oop = this->ui->oopRangeText->toPlainText().toStdString();
    qSolverJob->board = this->ui->boardText->toPlainText().toStdString();

    vector<string> board_str_arr = string_split(qSolverJob->board,',');
    if(board_str_arr.size() == 3){
        qSolverJob->current_round = 1;
    }else if(board_str_arr.size() == 4){
        qSolverJob->current_round = 2;
    }else if(board_str_arr.size() == 5){
        qSolverJob->current_round = 3;
    }else{
        this->ui->logOutput->log_with_signal(QString::fromStdString(tfm::format("Error : board %s not recognized",qSolverJob->board)));
        return;
    }
    qSolverJob->raise_limit = this->ui->raiseLimitText->text().toInt();
    qSolverJob->ip_commit = this->ui->potText->text().toFloat() / 2;
    qSolverJob->oop_commit = this->ui->potText->text().toFloat() / 2;
    qSolverJob->stack = this->ui->effectiveStackText->text().toFloat() + qSolverJob->ip_commit;
    qSolverJob->mode = this->ui->mode_box->currentIndex() == 0 ? QSolverJob::Mode::HOLDEM:QSolverJob::Mode::SHORTDECK;
    qSolverJob->allin_threshold = this->ui->allinThresholdText->text().toFloat();
    qSolverJob->use_isomorphism = this->ui->useIsoCheck->isChecked();
    qSolverJob->use_halffloats =  this->ui->useHalfFloats_box->currentIndex();
    if (this->ui->solverTypeComboBox->currentData().toInt() == static_cast<int>(SolverEngine::GPU_HIP)) {
        qSolverJob->use_halffloats = this->ui->gpuFp16CheckBox->isChecked() ? 2 : 0;
    }

    StreetSetting gbs_flop_ip = StreetSetting(sizes_convert(ui->flop_ip_bet->text()),
                                              sizes_convert(ui->flop_ip_raise->text()),
                                              vector<float>{},
                                              ui->flop_ip_allin->isChecked()
                                              );
    StreetSetting gbs_turn_ip = StreetSetting(sizes_convert(ui->turn_ip_bet->text()),
                                              sizes_convert(ui->turn_ip_raise->text()),
                                              vector<float>{},
                                              ui->turn_ip_allin->isChecked()
                                              );
    StreetSetting gbs_river_ip = StreetSetting(sizes_convert(ui->river_ip_bet->text()),
                                              sizes_convert(ui->river_ip_raise->text()),
                                              vector<float>{},
                                              ui->river_ip_allin->isChecked()
                                              );

    StreetSetting gbs_flop_oop = StreetSetting(sizes_convert(ui->flop_oop_bet->text()),
                                              sizes_convert(ui->flop_oop_raise->text()),
                                              vector<float>{},
                                              ui->flop_oop_allin->isChecked()
                                              );
    StreetSetting gbs_turn_oop = StreetSetting(sizes_convert(ui->turn_oop_bet->text()),
                                              sizes_convert(ui->turn_oop_raise->text()),
                                              sizes_convert(ui->turn_oop_donk->text()),
                                              ui->turn_oop_allin->isChecked()
                                              );
    StreetSetting gbs_river_oop = StreetSetting(sizes_convert(ui->river_oop_bet->text()),
                                              sizes_convert(ui->river_oop_raise->text()),
                                              sizes_convert(ui->river_oop_donk->text()),
                                              ui->river_oop_allin->isChecked()
                                              );

    qSolverJob->gtbs = make_shared<GameTreeBuildingSettings>(gbs_flop_ip,gbs_turn_ip,gbs_river_ip,gbs_flop_oop,gbs_turn_oop,gbs_river_oop);
    qSolverJob->current_mission = QSolverJob::MissionType::BUILDTREE;
    qSolverJob->start();
}

void MainWindow::on_copyButtom_clicked()
{
    ui->flop_oop_bet->setText(ui->flop_ip_bet->text());
    ui->flop_oop_raise->setText(ui->flop_ip_raise->text());
    ui->flop_oop_allin->setChecked(ui->flop_ip_allin->isChecked());

    ui->turn_oop_bet->setText(ui->turn_ip_bet->text());
    ui->turn_oop_raise->setText(ui->turn_ip_raise->text());
    ui->turn_oop_allin->setChecked(ui->turn_ip_allin->isChecked());

    ui->river_oop_bet->setText(ui->river_ip_bet->text());
    ui->river_oop_raise->setText(ui->river_ip_raise->text());
    ui->river_oop_allin->setChecked(ui->river_ip_allin->isChecked());
}

void MainWindow::on_showResultButton_clicked()
{
    if (this->qSolverJob == nullptr || 
        this->qSolverJob->get_solver() == nullptr || 
        this->qSolverJob->get_solver()->getGameTree() == nullptr) 
    {
        QMessageBox::warning(this, tr("Warning"), tr("Please build the game tree first!"));
        return;
    }
    openStrategyExplorer(true);
}

void MainWindow::openStrategyExplorer(bool maximized)
{
    if (this->qSolverJob == nullptr || 
        this->qSolverJob->get_solver() == nullptr || 
        this->qSolverJob->get_solver()->getGameTree() == nullptr) 
    {
        return;
    }

    if (this->strategyExplorer) {
        this->strategyExplorer->raise();
        this->strategyExplorer->activateWindow();
        if (maximized) {
            QTimer::singleShot(100, this->strategyExplorer, &QWidget::showMaximized);
        } else {
            this->strategyExplorer->showNormal();
        }
        return;
    }

    this->strategyExplorer = new StrategyExplorer(nullptr, this->qSolverJob);
    this->strategyExplorer->setPreselectedCards(this->ui->preselectedTurnEdit->text(), this->ui->preselectedRiverEdit->text());
    this->strategyExplorer->setAttribute(Qt::WA_DeleteOnClose);
    
    connect(this->strategyExplorer, &QObject::destroyed, this, [this]() {
        this->strategyExplorer = nullptr;
    });

    if (maximized) {
        this->strategyExplorer->show();
        QTimer::singleShot(100, this->strategyExplorer, &QWidget::showMaximized);
    } else {
        this->strategyExplorer->show();
    }
}

void MainWindow::onSolverJobFinished()
{
    if (qSolverJob && (qSolverJob->current_mission == QSolverJob::MissionType::SOLVING || qSolverJob->current_mission == QSolverJob::MissionType::LOAD_SOLVE)) {
        openStrategyExplorer(true);
    }
}

void MainWindow::on_stopSolvingButton_clicked()
{
    if(this->qSolverJob != NULL){
        this->qSolverJob->stop();
    }
}

void MainWindow::on_ipRangeSelectButtom_clicked()
{
    QSolverJob::Mode mode = this->ui->mode_box->currentIndex() == 0 ? QSolverJob::Mode::HOLDEM:QSolverJob::Mode::SHORTDECK;
    this->rangeSelector = new RangeSelector(this->ui->ipRangeText,this,mode);
    rangeSelector->setAttribute(Qt::WA_DeleteOnClose);
    rangeSelector->show();
}

void MainWindow::on_oopRangeSelectButtom_clicked()
{
    QSolverJob::Mode mode = this->ui->mode_box->currentIndex() == 0 ? QSolverJob::Mode::HOLDEM:QSolverJob::Mode::SHORTDECK;
    this->rangeSelector = new RangeSelector(this->ui->oopRangeText,this,mode);
    rangeSelector->setAttribute(Qt::WA_DeleteOnClose);
    rangeSelector->show();
}

float iso_corh(QString board){
    vector<string> board_str_arr = string_split(board.toStdString(),',');
    vector<Card> initialBoard;
    for(string one_board_str:board_str_arr){
        initialBoard.push_back(Card(one_board_str));
    }
    float corh = 1;
    uint16_t color_hash[4];
    for(int i = 0;i < 4;i ++)color_hash[i] = 0;
    for (Card one_card:initialBoard) {
        int rankind = one_card.getCardInt() % 4;
        int suitind = one_card.getCardInt() / 4;
        color_hash[rankind] = color_hash[rankind] | (1 << suitind);
    }
    for(int i = 0;i < 4;i ++){
        for(int j = 0;j < i;j ++){
            if(color_hash[i] == color_hash[j]){
                corh = corh * 0.70;
                continue;
            }
        }
    }
    return corh;
}

void MainWindow::on_estimateMemoryButtom_clicked()
{
    long long memory_float = this->qSolverJob->estimate_tree_memory(this->ui->ipRangeText->toPlainText(),this->ui->oopRangeText->toPlainText(),this->ui->boardText->toPlainText());
    // float32 should take 4bytes
    float corh = 1;
    if(this->ui->useIsoCheck->isChecked()){
        corh =iso_corh(this->ui->boardText->toPlainText());
    }
    int use_halffloats = this->ui->useHalfFloats_box->currentIndex();
    if (this->ui->solverTypeComboBox->currentData().toInt() == static_cast<int>(SolverEngine::GPU_HIP)) {
        use_halffloats = this->ui->gpuFp16CheckBox->isChecked() ? 2 : 0;
        // Phase 1 Optimization: GPU no longer uses d_strategies, reducing trainable buffers by 33%
        memory_float = (long long)((double)memory_float * (2.0 / 3.0));
    }
    switch(use_halffloats){
    case 0:
        break;
    case 1:
        corh *= 0.75;
        break;
    case 2:
        corh *= 0.5;
        break;
    }
    float memory_mb = (float)memory_float / 1024 / 1024 * corh * 4 ;
    float memory_gb = (float)memory_float / 1024 / 1024 / 1024 * corh * 4;
    QString message;
    if(memory_gb == 0){
        message = tr("Please build tree first.");
    }else if(memory_gb < 1){
        message = tr("Estimated Memory Usage: ") + QString::number(memory_mb,'f',0) + tr(" Mb") +
                tr("\nRebuild tree to have changed optimization options take effect!");
    }else{
        message = tr("Estimated Memory Usage: ") + QString::number(memory_gb,'f',1) + tr(" Gb") +
                tr("\nRebuild tree to have changed optimization options take effect!");
    }

    // Dynamic RAM Comparison
    double ram_gb = 0.0;
#ifdef Q_OS_UNIX
    long long pages = sysconf(_SC_PHYS_PAGES);
    long long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        ram_gb = (double)(pages * page_size) / (1024.0 * 1024.0 * 1024.0);
    }
#endif

    if (ram_gb > 0.0 && memory_gb > 0.0) {
        if (memory_gb > ram_gb * 0.8) {
            message += tr("\n\nWARNING: Estimated memory usage (") + QString::number(memory_gb, 'f', 1) + tr(" GB) is very close to or exceeds your total system RAM (") +
                       QString::number(ram_gb, 'f', 1) + tr(" GB).\nWe strongly recommend selecting 'speed' (half-floats) or 'speed and accuracy' under 'Save memory at cost of' to avoid system instability or crash.");
        } else {
            message += tr("\n\nSystem RAM check: Your system has ") + QString::number(ram_gb, 'f', 1) + tr(" GB of total RAM, which is sufficient for this tree size.");
        }
    }

    qDebug().noquote() << message;
    QMessageBox msgBox;
    msgBox.setText(message);
    msgBox.exec();
}

void MainWindow::on_selectBoardButton_clicked()
{
    QSolverJob::Mode mode = this->ui->mode_box->currentIndex() == 0 ? QSolverJob::Mode::HOLDEM:QSolverJob::Mode::SHORTDECK;
    this->boardSelector = new boardselector(this->ui->boardText,mode,this);
    boardSelector->setAttribute(Qt::WA_DeleteOnClose);
    boardSelector->show();
}

void MainWindow::on_saveParameterButton_clicked()
{
    QString modeStr = this->ui->mode_box->currentText();
    QString boardStr = this->ui->boardText->toPlainText();
    QString stackStr = this->ui->effectiveStackText->text();
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    QString handInfo = "";
    if (this->m_currentHandId != -1) {
        QSqlDatabase d = DBManager::instance().db();
        if (d.isOpen()) {
            QSqlQuery q(d);
            q.prepare("SELECT s.site_name, t.tourney_no, h.hand_no FROM hands h "
                      "JOIN tourneys t ON h.id_tourney = t.id_tourney "
                      "JOIN sites s ON t.id_site = s.id_site "
                      "WHERE h.id_hand = :id");
            q.bindValue(":id", this->m_currentHandId);
            if (q.exec() && q.next()) {
                QString site = q.value(0).toString();
                QString tourney = q.value(1).toString();
                QString hand = q.value(2).toString();
                handInfo = QString("%1 T%2 H%3 - ").arg(site).arg(tourney).arg(hand);
            }
        }
    }

    QString name = QString("%1 - %2%3 - %4 - Stack: %5")
        .arg(timeStr)
        .arg(handInfo)
        .arg(modeStr)
        .arg(boardStr.isEmpty() ? "No Board" : boardStr)
        .arg(stackStr);

    QString paramsText = this->export_to_string();
    int id = DBManager::instance().saveParameter(name, paramsText);
    if (id != -1) {
        // Update table
        QSqlQueryModel* oldModel = qParametersModel;
        qParametersModel = DBManager::instance().getParametersModel(this);
        this->ui->parametersTableView->setModel(qParametersModel);
        if (oldModel) oldModel->deleteLater();
        this->ui->parametersTableView->hideColumn(0);
    } else {
        QMessageBox::warning(this, "Save Failed", "Failed to save parameter. Does a parameter with this name already exist?");
    }
}

void MainWindow::on_loadParameterButton_clicked()
{
    QModelIndexList selection = this->ui->parametersTableView->selectionModel()->selectedRows();
    if (selection.isEmpty()) return;

    int id = qParametersModel->record(selection.at(0).row()).value("ID").toInt();
    QString text;
    if (DBManager::instance().loadParameter(id, text)) {
        this->import_from_content(text);
    }
}

void MainWindow::on_deleteParameterButton_clicked()
{
    QModelIndexList selection = this->ui->parametersTableView->selectionModel()->selectedRows();
    if (selection.isEmpty()) return;

    int id = qParametersModel->record(selection.at(0).row()).value("ID").toInt();
    if (DBManager::instance().deleteParameter(id)) {
        // Update table
        QSqlQueryModel* oldModel = qParametersModel;
        qParametersModel = DBManager::instance().getParametersModel(this);
        this->ui->parametersTableView->setModel(qParametersModel);
        if (oldModel) oldModel->deleteLater();
        this->ui->parametersTableView->hideColumn(0);
    }
}

void MainWindow::on_parametersTableView_doubleClicked(const QModelIndex& index)
{
    on_loadParameterButton_clicked();
}

void MainWindow::solve_item_clicked(const QModelIndex& index)
{
    // Optionally preview metadata or load on click
}

void MainWindow::on_openSolvesFolderButton_clicked()
{
    QString path = QDir::current().filePath("solves");
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}



void MainWindow::on_saveSolveButton_clicked()
{
    if (!this->qSolverJob || !this->qSolverJob->get_solver() || !this->qSolverJob->get_solver()->getGameTree()) {
        QMessageBox::warning(this, "Error", "No active solve to save!");
        return;
    }

    // Extract metadata
    QString handHistoryText = "";
    HandMetadata md = HandHistoryParser::extractMetadata(handHistoryText);
    json md_json;
    md_json["metadata"] = {
        {"site", md.site.toStdString()},
        {"hand_id", md.hand_id.toStdString()},
        {"tournament_id", md.tournament_id.toStdString()},
        {"stakes", md.stakes.toStdString()}
    };
    std::string metadataStr = md_json.dump();

    QString tempPath = QDir::temp().filePath("temp_solve.txs");

    bool success = this->qSolverJob->get_solver()->get_solver()->save_solve_to_file(
        tempPath.toStdString(),
        this->export_to_string().toStdString(),
        metadataStr
    );

    if (!success) {
        QMessageBox::warning(this, "Error", "Failed to write compressed binary data.");
        return;
    }

    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error", "Failed to read compressed temp file.");
        return;
    }
    QByteArray blob = tempFile.readAll();
    tempFile.close();
    QFile::remove(tempPath);

    double exp = this->qSolverJob->get_solver()->get_solver()->last_exploitability;
    QVariant dbHandId = QVariant(); // Null
    if (this->m_currentHandId != -1) {
        dbHandId = this->m_currentHandId;
    }

    bool is_node_locked = !this->qSolverJob->locked_strategies.empty();
    int new_id = DBManager::instance().saveSolve(dbHandId, this->export_to_string(), exp, blob, is_node_locked, m_currentParentSolveId);
    
    if (new_id < 0) {
        QMessageBox::warning(this, "Error", "Failed to save solve to database.");
    } else {
        m_currentParentSolveId = new_id;
        QMessageBox::information(this, "Success", "Solve saved to database successfully!");
        if (qSolvesModel) {
            QSqlQueryModel* newModel = DBManager::instance().getSolvesModel(this);
            if (newModel) {
                this->ui->solvesTreeView->setModel(newModel);
                this->ui->solvesTreeView->hideColumn(0); // Hide ID column
                this->ui->solvesTreeView->resizeColumnsToContents();
                delete qSolvesModel;
                qSolvesModel = newModel;
            }
        }
    }
}



void MainWindow::on_loadSolveButton_clicked()
{
    QModelIndex index = this->ui->solvesTreeView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "Error", "Please select a valid solve to load.");
        return;
    }
    
    int solveId = qSolvesModel->record(index.row()).value("Solve ID").toInt();
    qDebug() << "Trying to load solve ID:" << solveId << "from row:" << index.row();
    m_currentParentSolveId = solveId;

    QString configText;
    double exp;
    QByteArray strategyBlob;
    if (!DBManager::instance().loadSolveById(solveId, configText, exp, strategyBlob)) {
        QMessageBox::warning(this, "Error", "Failed to load solve from DB.");
        return;
    }

    QString tempPath = QDir::temp().filePath("temp_load.txs");
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Error", "Failed to write temp file.");
        return;
    }
    tempFile.write(strategyBlob);
    tempFile.close();

    std::string configStr, metadataStr;
    if (!Solver::read_config_from_file(tempPath.toStdString(), configStr, metadataStr)) {
        QMessageBox::warning(this, "Error", "Failed to read solve file.");
        QFile::remove(tempPath);
        return;
    }

    try {
        // 1. Rebuild UI Config
        this->import_from_content(QString::fromStdString(configStr));

        // Apply to current window
        if (this->strategyExplorer) {
            delete this->strategyExplorer;
            this->strategyExplorer = nullptr;
        }

        PokerSolver old_holdem;
        PokerSolver old_shortdeck;
        if (this->qSolverJob) {
            this->qSolverJob->stop();
            this->qSolverJob->exit();
            this->qSolverJob->wait();
            old_holdem = this->qSolverJob->ps_holdem;
            old_shortdeck = this->qSolverJob->ps_shortdeck;
            delete this->qSolverJob;
        }

        this->qSolverJob = new QSolverJob;
        this->qSolverJob->ps_holdem = old_holdem;
        this->qSolverJob->ps_shortdeck = old_shortdeck;
        connect(qSolverJob, &QThread::finished, this, &MainWindow::onSolverJobFinished);
        this->qSolverJob->setContext(this->getLogArea());
        
        this->qSolverJob->clearAllLocks();
        this->qSolverJob->range_ip = this->ui->ipRangeText->toPlainText().toStdString();
        this->qSolverJob->range_oop = this->ui->oopRangeText->toPlainText().toStdString();
        this->qSolverJob->board = this->ui->boardText->toPlainText().toStdString();

        vector<string> board_str_arr = string_split(this->qSolverJob->board,',');
        if(board_str_arr.size() == 3){
            this->qSolverJob->current_round = 1;
        }else if(board_str_arr.size() == 4){
            this->qSolverJob->current_round = 2;
        }else if(board_str_arr.size() == 5){
            this->qSolverJob->current_round = 3;
        }else{
            this->ui->logOutput->log_with_signal(QString::fromStdString(tfm::format("Error : board %s not recognized",this->qSolverJob->board)));
            return;
        }
        this->qSolverJob->raise_limit = this->ui->raiseLimitText->text().toInt();
        this->qSolverJob->ip_commit = this->ui->potText->text().toFloat() / 2;
        this->qSolverJob->oop_commit = this->ui->potText->text().toFloat() / 2;
        this->qSolverJob->stack = this->ui->effectiveStackText->text().toFloat() + this->qSolverJob->ip_commit;
        this->qSolverJob->mode = this->ui->mode_box->currentIndex() == 0 ? QSolverJob::Mode::HOLDEM:QSolverJob::Mode::SHORTDECK;
        this->qSolverJob->allin_threshold = this->ui->allinThresholdText->text().toFloat();
        this->qSolverJob->use_isomorphism = this->ui->useIsoCheck->isChecked();
        this->qSolverJob->use_halffloats =  this->ui->useHalfFloats_box->currentIndex();
        if (this->ui->solverTypeComboBox->currentData().toInt() == static_cast<int>(SolverEngine::GPU_HIP)) {
            this->qSolverJob->use_halffloats = this->ui->gpuFp16CheckBox->isChecked() ? 2 : 0;
        }

        int threads = this->ui->threadsText->text().toInt();
        int selectedSolver = this->ui->solverTypeComboBox->currentData().toInt();
        this->qSolverJob->engineType = static_cast<SolverEngine>(selectedSolver);
        this->qSolverJob->max_iteration = this->ui->iterationText->text().toInt();
        this->qSolverJob->accuracy = this->ui->exploitabilityText->text().toFloat();
        this->qSolverJob->print_interval = this->ui->logIntervalText->text().toInt();
        this->qSolverJob->thread_number = threads;

        StreetSetting gbs_flop_ip = StreetSetting(sizes_convert(ui->flop_ip_bet->text()),
                                                  sizes_convert(ui->flop_ip_raise->text()),
                                                  vector<float>{},
                                                  ui->flop_ip_allin->isChecked()
                                                  );
        StreetSetting gbs_turn_ip = StreetSetting(sizes_convert(ui->turn_ip_bet->text()),
                                                  sizes_convert(ui->turn_ip_raise->text()),
                                                  vector<float>{},
                                                  ui->turn_ip_allin->isChecked()
                                                  );
        StreetSetting gbs_river_ip = StreetSetting(sizes_convert(ui->river_ip_bet->text()),
                                                  sizes_convert(ui->river_ip_raise->text()),
                                                  vector<float>{},
                                                  ui->river_ip_allin->isChecked()
                                                  );

        StreetSetting gbs_flop_oop = StreetSetting(sizes_convert(ui->flop_oop_bet->text()),
                                                  sizes_convert(ui->flop_oop_raise->text()),
                                                  vector<float>{},
                                                  ui->flop_oop_allin->isChecked()
                                                  );
        StreetSetting gbs_turn_oop = StreetSetting(sizes_convert(ui->turn_oop_bet->text()),
                                                  sizes_convert(ui->turn_oop_raise->text()),
                                                  sizes_convert(ui->turn_oop_donk->text()),
                                                  ui->turn_oop_allin->isChecked()
                                                  );
        StreetSetting gbs_river_oop = StreetSetting(sizes_convert(ui->river_oop_bet->text()),
                                                  sizes_convert(ui->river_oop_raise->text()),
                                                  sizes_convert(ui->river_oop_donk->text()),
                                                  ui->river_oop_allin->isChecked()
                                                  );

        this->qSolverJob->gtbs = make_shared<GameTreeBuildingSettings>(gbs_flop_ip,gbs_turn_ip,gbs_river_ip,gbs_flop_oop,gbs_turn_oop,gbs_river_oop);

        this->qSolverJob->solve_filepath = tempPath.toStdString();
        this->qSolverJob->loaded_exploitability = exp;
        this->qSolverJob->current_mission = QSolverJob::MissionType::LOAD_SOLVE; 
        
        QMessageBox::information(this, "Loading", "Solve loading started in background thread!");
        this->qSolverJob->start();

    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to load solve: %1").arg(e.what()));
    }
}

void MainWindow::on_deleteSolveButton_clicked()
{
    QModelIndex index = this->ui->solvesTreeView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "Error", "Please select a valid solve to delete.");
        return;
    }
    
    int solveId = qSolvesModel->record(index.row()).value("Solve ID").toInt();
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Delete Solve", "Are you sure you want to delete this solve? This cannot be undone.", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No) {
        return;
    }

    if (DBManager::instance().deleteSolve(solveId)) {
        // Refresh table
        if (qSolvesModel) {
            QSqlQueryModel* newModel = DBManager::instance().getSolvesModel(this);
            if (newModel) {
                this->ui->solvesTreeView->setModel(newModel);
                this->ui->solvesTreeView->hideColumn(0); // Hide ID column
                this->ui->solvesTreeView->resizeColumnsToContents();
                delete qSolvesModel;
                qSolvesModel = newModel;
            }
        }
        QMessageBox::information(this, "Success", "Solve deleted successfully.");
    }
}

// Removed export current parameter button

void MainWindow::on_ipRangeText_textChanged()
{
    this->ip_model->setRangeText(this->ui->ipRangeText->toPlainText());
    this->ui->IpRangeTableView->update();
}

void MainWindow::on_oopRangeText_textChanged()
{
    this->oop_model->setRangeText(this->ui->oopRangeText->toPlainText());
    this->ui->oopRangeTableView->update();
}

// Removed onExpanded

void MainWindow::on_actionPreflopSolver_triggered()
{
    if (this->preflopSolverWindow) {
        this->preflopSolverWindow->raise();
        this->preflopSolverWindow->activateWindow();
        QTimer::singleShot(100, this->preflopSolverWindow, &QWidget::showMaximized);
        return;
    }
    this->preflopSolverWindow = new PreflopSolverWindow(this->qSolverJob, this);
    this->preflopSolverWindow->setAttribute(Qt::WA_DeleteOnClose);
    connect(this->preflopSolverWindow, &QObject::destroyed, this, [this]() {
        this->preflopSolverWindow = nullptr;
    });
    this->preflopSolverWindow->show();
    QTimer::singleShot(100, this->preflopSolverWindow, &QWidget::showMaximized);
}
