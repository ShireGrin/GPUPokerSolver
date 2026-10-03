#include "gtotrainerwindow.h"
#include "ui_gtotrainerwindow.h"
#include "strategyexplorer.h"
#include "include/tools/dbmanager.h"
#include <QTimer>
#include <QTime>
#include <QScrollBar>
#include <QTextCursor>
#include <algorithm>
#include <cmath>
#include "include/library.h"
#include <iostream>


GtoTrainerWindow::GtoTrainerWindow(QWidget *parent, QSolverJob *qSolverJob, shared_ptr<GameTreeNode> startNode) :
    QDialog(parent),
    ui(new Ui::GtoTrainerWindow),
    qSolverJob(qSolverJob),
    m_startNode(startNode),
    m_startNodeItem(nullptr),
    m_currentNodeItem(nullptr),
    handsPlayed(0),
    correctActions(0),
    totalActions(0),
    totalEvLoss(0.0f),
    sessionActive(false),
    m_streetTransitionOccurred(false),
    m_streetStartRangeWeightSum(0.0f),
    m_quizCorrect(0),
    m_quizTotal(0),
    m_narrowingCorrect(0),
    m_narrowingTotal(0),
    rangeChallengeTotal(0),
    rangeChallengeAccuracySum(0.0),
    m_updatingSliders(false)
{
    ui->setupUi(this);

    // Connect Range Challenge checkbox toggled signal
    connect(ui->rangeChallengeCheckBox, &QCheckBox::toggled, ui->rangeChallengeModeComboBox, &QComboBox::setEnabled);

    // Populate role selections
    ui->roleComboBox->addItem("Play as IP (Player 1)");
    ui->roleComboBox->addItem("Play as OOP (Player 2)");
    ui->roleComboBox->addItem("Play as Both (Random)");
    ui->roleComboBox->setCurrentIndex(2);

    // Populate opponent profiles
    ui->profileComboBox->addItem("Perfect GTO");
    ui->profileComboBox->addItem("Calling Station");
    ui->profileComboBox->addItem("Nit");
    ui->profileComboBox->addItem("Maniac");
    ui->profileComboBox->setCurrentIndex(0);

    // Populate start spot selections
    ui->startNodeComboBox->addItem("Start from Root Node");
    if (qSolverJob && qSolverJob->get_solver() && qSolverJob->get_solver()->getGameTree() && qSolverJob->get_solver()->getGameTree()->getRoot()) {
        if (m_startNode && m_startNode != qSolverJob->get_solver()->getGameTree()->getRoot()) {
            ui->startNodeComboBox->addItem("Start from Selected Spot");
            ui->startNodeComboBox->setCurrentIndex(0);
        } else {
            ui->startNodeComboBox->setCurrentIndex(0);
            m_startNode = qSolverJob->get_solver()->getGameTree()->getRoot();
        }
    } else {
        ui->startNodeComboBox->setCurrentIndex(0);
        m_startNode = nullptr;
    }

    // Populate cardint2card lookup map
    if (qSolverJob && qSolverJob->get_solver() && qSolverJob->get_solver()->get_deck()) {
        for (Card one_card : qSolverJob->get_solver()->get_deck()->getCards()) {
            cardint2card[one_card.getCardInt()] = one_card;
        }
    }

    // Initialize side-by-side range views
    p1TableModel = new TableStrategyModel(qSolverJob, this);
    p1Setting.mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
    p1Setting.targetPlayer = 0;
    p1Delegate = new StrategyItemDelegate(qSolverJob, &p1Setting, this);
    ui->p1RangeView->setModel(p1TableModel);
    ui->p1RangeView->setItemDelegate(p1Delegate);
    ui->p1RangeView->setAutoSizeRows(true);
    ui->p1RangeView->setMinRowHeight(25);

    p2TableModel = new TableStrategyModel(qSolverJob, this);
    p2Setting.mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;
    p2Setting.targetPlayer = 1;
    p2Delegate = new StrategyItemDelegate(qSolverJob, &p2Setting, this);
    ui->p2RangeView->setModel(p2TableModel);
    ui->p2RangeView->setItemDelegate(p2Delegate);
    ui->p2RangeView->setAutoSizeRows(true);
    ui->p2RangeView->setMinRowHeight(25);

    p1ButtonGroup = new QButtonGroup(this);
    p2ButtonGroup = new QButtonGroup(this);

    // Hook up buttons
    connect(ui->startSessionButton, &QPushButton::clicked, this, &GtoTrainerWindow::onStartSessionButtonClicked);
    connect(ui->nextHandButton, &QPushButton::clicked, this, &GtoTrainerWindow::onNextHandButtonClicked);
    connect(ui->viewModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        DetailWindowSetting::DetailWindowMode p1Mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
        DetailWindowSetting::DetailWindowMode p2Mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;

        if (m_currentNode && m_currentNode->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
            shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
            int playerToAct = actionNode->getPlayer();

            if (index == 1) {
                if (playerToAct == 0) {
                    p1Mode = DetailWindowSetting::DetailWindowMode::STRATEGY;
                    p2Mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;
                } else {
                    p1Mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
                    p2Mode = DetailWindowSetting::DetailWindowMode::STRATEGY;
                }
            } else if (index == 2) {
                if (playerToAct == 0) {
                    p1Mode = DetailWindowSetting::DetailWindowMode::EV_ONLY;
                    p2Mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;
                } else {
                    p1Mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
                    p2Mode = DetailWindowSetting::DetailWindowMode::EV_ONLY;
                }
            }
        }

        p1Setting.mode = p1Mode;
        p2Setting.mode = p2Mode;
        ui->p1RangeView->viewport()->update();
        ui->p2RangeView->viewport()->update();
    });

    ui->nextHandButton->hide();

    // Get parent turn and river cards
    StrategyExplorer* explorer = qobject_cast<StrategyExplorer*>(parent);
    if (explorer) {
        TableStrategyModel* model = explorer->getTableStrategyModel();
        if (model) {
            turnCard = model->getTrunCard();
            riverCard = model->getRiverCard();
        }
    }

    p1TableModel->setTrunCard(turnCard);
    p1TableModel->setRiverCard(riverCard);
    p2TableModel->setTrunCard(turnCard);
    p2TableModel->setRiverCard(riverCard);
}

GtoTrainerWindow::~GtoTrainerWindow()
{
    if (ui) {
        if (ui->p1RangeView) ui->p1RangeView->setModel(nullptr);
        if (ui->p2RangeView) ui->p2RangeView->setModel(nullptr);
    }
    if (p1TableModel) p1TableModel->setGameTreeNode(nullptr);
    if (p2TableModel) p2TableModel->setGameTreeNode(nullptr);

    if (m_startNodeItem) {
        delete m_startNodeItem;
        m_startNodeItem = nullptr;
    }
    if (m_currentNodeItem) {
        delete m_currentNodeItem;
        m_currentNodeItem = nullptr;
    }
    delete ui;
}

void GtoTrainerWindow::onStartSessionButtonClicked()
{
    if (sessionActive) {
        // Stop Session
        sessionActive = false;
        ui->roleComboBox->setEnabled(true);
        ui->profileComboBox->setEnabled(true);
        ui->startNodeComboBox->setEnabled(true);
        ui->rangeChallengeCheckBox->setEnabled(true);
        if (ui->rangeChallengeCheckBox->isChecked()) {
            ui->rangeChallengeModeComboBox->setEnabled(true);
        }
        ui->startSessionButton->setText("Start Session");
        ui->nextHandButton->hide();
        ui->feedbackLabel->clear();
        ui->feedbackLabel->setStyleSheet("");
        ui->feedbackLabel->hide();
        clearLayout(ui->actionButtonsLayout);
        logMessage("Session stopped.");
        if (ui->tableVisualizer) ui->tableVisualizer->clearState();
    } else {
        // Start Session
        sessionActive = true;
        ui->roleComboBox->setEnabled(false);
        ui->profileComboBox->setEnabled(false);
        ui->startNodeComboBox->setEnabled(false);
        ui->rangeChallengeCheckBox->setEnabled(false);
        ui->rangeChallengeModeComboBox->setEnabled(false);
        ui->startSessionButton->setText("Stop Session");

        userPlayerSelection = ui->roleComboBox->currentIndex();
        oppProfile = static_cast<OpponentProfile>(ui->profileComboBox->currentIndex());

        if (!qSolverJob || !qSolverJob->get_solver() || !qSolverJob->get_solver()->getGameTree() || !qSolverJob->get_solver()->getGameTree()->getRoot()) {
            logMessage("Error: Solver or Game Tree not initialized.");
            sessionActive = false;
            ui->roleComboBox->setEnabled(true);
            ui->profileComboBox->setEnabled(true);
            ui->startNodeComboBox->setEnabled(true);
            ui->rangeChallengeCheckBox->setEnabled(true);
            if (ui->rangeChallengeCheckBox->isChecked()) {
                ui->rangeChallengeModeComboBox->setEnabled(true);
            }
            ui->startSessionButton->setText("Start Session");
            return;
        }

        if (ui->startNodeComboBox->currentIndex() == 0) {
            m_startNode = qSolverJob->get_solver()->getGameTree()->getRoot();
        } // Else it stays the selected spot passed in constructor

        handsPlayed = 0;
        correctActions = 0;
        totalActions = 0;
        totalEvLoss = 0.0f;
        m_quizCorrect = 0;
        m_quizTotal = 0;
        m_narrowingCorrect = 0;
        m_narrowingTotal = 0;
        rangeChallengeTotal = 0;
        rangeChallengeAccuracySum = 0.0;

        updateStatsLabel();
        ui->actionLog->clear();

        logMessage("Session started!");

        startNewHand();
    }
}

void GtoTrainerWindow::startNewHand()
{
    if (!qSolverJob || !qSolverJob->get_solver() || !qSolverJob->get_solver()->getGameTree() || !m_startNode) {
        logMessage("Error: Game tree or start node not initialized.");
        return;
    }

    ui->nextHandButton->hide();
    ui->feedbackLabel->clear();
    ui->feedbackLabel->setStyleSheet("");
    ui->feedbackLabel->hide();

    handsPlayed++;

    // Read active turn/river card from strategy explorer
    StrategyExplorer* explorer = qobject_cast<StrategyExplorer*>(parent());
    if (explorer) {
        TableStrategyModel* model = explorer->getTableStrategyModel();
        if (model) {
            turnCard = model->getTrunCard();
            riverCard = model->getRiverCard();
        }
    }

    p1TableModel->setTrunCard(turnCard);
    p1TableModel->setRiverCard(riverCard);
    p2TableModel->setTrunCard(turnCard);
    p2TableModel->setRiverCard(riverCard);

    activeBoard = getBoardAtNode(m_startNode);

    // Determine active role
    if (userPlayerSelection == 2) {
        activeUserRole = std::rand() % 2;
    } else {
        activeUserRole = userPlayerSelection;
    }

    // Set start node treeitem for range calculations
    if (p1TableModel) p1TableModel->setGameTreeNode(nullptr);
    if (p2TableModel) p2TableModel->setGameTreeNode(nullptr);
    if (m_startNodeItem) {
        delete m_startNodeItem;
        m_startNodeItem = nullptr;
    }
    m_startNodeItem = new TreeItem(m_startNode);
    p1TableModel->setGameTreeNode(m_startNodeItem);
    p1TableModel->updateStrategyData();
    p2TableModel->setGameTreeNode(m_startNodeItem);
    p2TableModel->updateStrategyData();

    // Sample P1 (IP) private cards
    std::vector<PrivateCards>& p1range = qSolverJob->get_solver()->player1Range;
    std::vector<PrivateCards> validP1;
    std::vector<float> p1Weights;
    for (auto& pc : p1range) {
        bool blocked = false;
        for (Card& bc : activeBoard) {
            if (bc.getCardInt() == pc.card1 || bc.getCardInt() == pc.card2) {
                blocked = true;
                break;
            }
        }
        float w = 0.0f;
        if (pc.card1 < (int)p1TableModel->p1_range.size() && pc.card2 < (int)p1TableModel->p1_range[pc.card1].size()) {
            w = p1TableModel->p1_range[pc.card1][pc.card2];
        }
        if (!blocked && w > 0.0f) {
            validP1.push_back(pc);
            p1Weights.push_back(w);
        }
    }

    if (validP1.empty()) {
        logMessage("Error: No valid hands in P1 range.");
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::discrete_distribution<int> distP1(p1Weights.begin(), p1Weights.end());
    PrivateCards sampledP1 = validP1[distP1(gen)];
    Card p1Card1 = cardint2card[sampledP1.card1];
    Card p1Card2 = cardint2card[sampledP1.card2];

    // Sample P2 (OOP) private cards
    std::vector<PrivateCards>& p2range = qSolverJob->get_solver()->player2Range;
    std::vector<PrivateCards> validP2;
    std::vector<float> p2Weights;
    for (auto& pc : p2range) {
        bool blocked = false;
        for (Card& bc : activeBoard) {
            if (bc.getCardInt() == pc.card1 || bc.getCardInt() == pc.card2) {
                blocked = true;
                break;
            }
        }
        if (sampledP1.card1 == pc.card1 || sampledP1.card1 == pc.card2 ||
            sampledP1.card2 == pc.card1 || sampledP1.card2 == pc.card2) {
            blocked = true;
        }
        float w = 0.0f;
        if (pc.card1 < (int)p2TableModel->p2_range.size() && pc.card2 < (int)p2TableModel->p2_range[pc.card1].size()) {
            w = p2TableModel->p2_range[pc.card1][pc.card2];
        }
        if (!blocked && w > 0.0f) {
            validP2.push_back(pc);
            p2Weights.push_back(w);
        }
    }

    if (validP2.empty()) {
        logMessage("Error: No valid hands in P2 range.");
        return;
    }

    std::discrete_distribution<int> distP2(p2Weights.begin(), p2Weights.end());
    PrivateCards sampledP2 = validP2[distP2(gen)];
    Card p2Card1 = cardint2card[sampledP2.card1];
    Card p2Card2 = cardint2card[sampledP2.card2];

    // Assign based on activeUserRole
    // player 0 is IP, player 1 is OOP.
    if (activeUserRole == 0) {
        userCard1 = p1Card1; userCard2 = p1Card2;
        oppCard1 = p2Card1; oppCard2 = p2Card2;
    } else {
        userCard1 = p2Card1; userCard2 = p2Card2;
        oppCard1 = p1Card1; oppCard2 = p1Card2;
    }

    if (userPlayerSelection == 2) {
        // play as both
    } else {
        std::vector<Card> handVec = {userCard1, userCard2};
    }

    logMessage(QString("--- New Hand (#%1) ---").arg(handsPlayed));
    logMessage(QString("You are playing as %1").arg(activeUserRole == 0 ? "IP" : "OOP"));
    if (ui->rangeChallengeCheckBox->isChecked()) {
        logMessage("Your Hand: [ Entire Range ]");
    } else {
        logMessage(QString("Your Hand: %1 %2").arg(QString::fromStdString(userCard1.toFormattedString())).arg(QString::fromStdString(userCard2.toFormattedString())));
    }

    m_streetStartRangeWeightSum = computeRangeWeightSum(1 - activeUserRole);
    m_streetTransitionOccurred = false;

    m_currentNode = m_startNode;

    // Log path from root to m_currentNode to populate the action history
    shared_ptr<GameTreeNode> root = qSolverJob->get_solver()->getGameTree()->getRoot();
    if (m_currentNode != root) {
        std::vector<shared_ptr<GameTreeNode>> path;
        shared_ptr<GameTreeNode> curr = m_currentNode;
        while (curr) {
            path.push_back(curr);
            if (curr == root) {
                break;
            }
            curr = curr->getParent();
        }
        std::reverse(path.begin(), path.end());

        for (size_t i = 0; i < path.size() - 1; ++i) {
            // Check for round transitions
            if (path[i+1]->getRound() != path[i]->getRound()) {
                GameTreeNode::GameRound nextRound = path[i+1]->getRound();
                if (nextRound == GameTreeNode::GameRound::TURN && !turnCard.empty()) {
                    logMessage(QString("Dealing Turn card: <b>%1</b>").arg(QString::fromStdString(turnCard.toFormattedString())));
                } else if (nextRound == GameTreeNode::GameRound::RIVER && !riverCard.empty()) {
                    logMessage(QString("Dealing River card: <b>%1</b>").arg(QString::fromStdString(riverCard.toFormattedString())));
                }
            }

            // Check for actions
            if (path[i]->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
                shared_ptr<ActionNode> actionParent = dynamic_pointer_cast<ActionNode>(path[i]);
                int playerIdx = actionParent->getPlayer();
                int idx = -1;
                for (size_t j = 0; j < actionParent->getChildrens().size(); ++j) {
                    if (actionParent->getChildrens()[j] == path[i+1]) {
                        idx = j;
                        break;
                    }
                }
                if (idx != -1) {
                    GameActions chosenAction = actionParent->getActions()[idx];
                    QString actName = QString::fromStdString(chosenAction.toString());
                    actName.replace("BET_", "Bet ");
                    actName.replace("RAISE_", "Raise ");
                    actName.replace("CALL", "Call");
                    actName.replace("CHECK", "Check");
                    actName.replace("FOLD", "Fold");

                    QString playerStr = (playerIdx == activeUserRole ? "You" : "Opponent");
                    logMessage(QString("%1 chose: <b>%2</b>").arg(playerStr, actName));
                }
            }
        }
    }



    processGameState();
}

void GtoTrainerWindow::processGameState()
{
    if (!m_currentNode) return;

    if (ui->tableVisualizer) {
        ui->tableVisualizer->updateState(buildCurrentGameState());
    }

    double remaining_stack = std::min(getPlayerStacks(m_currentNode).first, getPlayerStacks(m_currentNode).second);

    // Removed top-left pot/board text updates since Visualizer replaces them

    // Handle chance nodes
    if (m_currentNode->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        shared_ptr<ChanceNode> chanceNode = dynamic_pointer_cast<ChanceNode>(m_currentNode);
        shared_ptr<GameTreeNode> child = chanceNode->getChildren();

        // Update table models to ChanceNode to get the current range before dealing the next card
        if (m_currentNodeItem) {
            delete m_currentNodeItem;
            m_currentNodeItem = nullptr;
        }
        m_currentNodeItem = new TreeItem(m_currentNode);
        p1TableModel->setGameTreeNode(m_currentNodeItem);
        p1TableModel->updateStrategyData();
        p2TableModel->setGameTreeNode(m_currentNodeItem);
        p2TableModel->updateStrategyData();

        auto dealAndAdvance = [this, child]() {
            // Sample turn or river card
            std::vector<Card> remainingDeck;
            for (auto& pair : cardint2card) {
                Card c = pair.second;
                bool used = false;
                for (Card& bc : activeBoard) {
                    if (bc.getCardInt() == c.getCardInt()) { used = true; break; }
                }
                if (userCard1.getCardInt() == c.getCardInt() || userCard2.getCardInt() == c.getCardInt() ||
                    oppCard1.getCardInt() == c.getCardInt() || oppCard2.getCardInt() == c.getCardInt()) {
                    used = true;
                }
                if (!used) remainingDeck.push_back(c);
            }

            if (remainingDeck.empty()) {
                logMessage("Error: No cards remaining in deck.");
                return;
            }

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dis(0, remainingDeck.size() - 1);
            Card dealtCard = remainingDeck[dis(gen)];
            activeBoard.push_back(dealtCard);

            if (child->getRound() == GameTreeNode::GameRound::TURN) {
                turnCard = dealtCard;
                logMessage(QString("Dealing Turn card: <b>%1</b>").arg(QString::fromStdString(dealtCard.toFormattedString())));
            } else if (child->getRound() == GameTreeNode::GameRound::RIVER) {
                riverCard = dealtCard;
                logMessage(QString("Dealing River card: <b>%1</b>").arg(QString::fromStdString(dealtCard.toFormattedString())));
            }

            p1TableModel->setTrunCard(turnCard);
            p1TableModel->setRiverCard(riverCard);
            p2TableModel->setTrunCard(turnCard);
            p2TableModel->setRiverCard(riverCard);

            m_streetTransitionOccurred = true;
            m_currentNode = child;
            processGameState();
        };

        if (ui->narrowingCheckBox->isChecked()) {
            int opponentPlayer = 1 - activeUserRole;
            // Only show the challenge if the range actually narrowed
            float currentWeightSum = computeRangeWeightSum(opponentPlayer);
            float ratio = (m_streetStartRangeWeightSum > 0.0001f)
                          ? (currentWeightSum / m_streetStartRangeWeightSum)
                          : 1.0f;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;

            if (ratio < 0.99f) {
                showNarrowingChallenge(opponentPlayer, dealAndAdvance);
            } else {
                dealAndAdvance();
            }
        } else {
            dealAndAdvance();
        }
        return;
    }

    // Handle terminal nodes
    if (m_currentNode->getType() == GameTreeNode::GameTreeNodeType::TERMINAL) {
        shared_ptr<TerminalNode> termNode = dynamic_pointer_cast<TerminalNode>(m_currentNode);
        std::vector<double> payoffs = termNode->get_payoffs();

        double userPayoff = payoffs[activeUserRole];
        logMessage(QString("Hand complete! Payoff: You %1 BB")
                   .arg(userPayoff >= 0 ? "+" + QString::number(userPayoff, 'f', 2) : QString::number(userPayoff, 'f', 2)));

        ui->nextHandButton->setText("Next Hand ➡️");
        ui->nextHandButton->show();
        clearLayout(ui->actionButtonsLayout);
        return;
    }

    // Handle showdown nodes
    if (m_currentNode->getType() == GameTreeNode::GameTreeNodeType::SHOWDOWN) {
        shared_ptr<ShowdownNode> showdownNode = dynamic_pointer_cast<ShowdownNode>(m_currentNode);

        // Evaluate winner
        std::vector<Card> p1Cards = { cardint2card[activeUserRole == 0 ? userCard1.getCardInt() : oppCard1.getCardInt()], cardint2card[activeUserRole == 0 ? userCard2.getCardInt() : oppCard2.getCardInt()] };
        std::vector<Card> p2Cards = { cardint2card[activeUserRole == 1 ? userCard1.getCardInt() : oppCard1.getCardInt()], cardint2card[activeUserRole == 1 ? userCard2.getCardInt() : oppCard2.getCardInt()] };
        
        shared_ptr<Dic5Compairer> compairer = qSolverJob->get_solver()->get_compairer();
        Compairer::CompairResult res = compairer->compair(p1Cards, p2Cards, activeBoard);
        
        int winner = 0;
        ShowdownNode::ShowDownResult showDownRes = ShowdownNode::ShowDownResult::NOTTIE;
        if (res == Compairer::CompairResult::LARGER) {
            winner = 0;
            logMessage("Showdown! Player 1 (IP) wins.");
        } else if (res == Compairer::CompairResult::SMALLER) {
            winner = 1;
            logMessage("Showdown! Player 2 (OOP) wins.");
        } else {
            winner = -1;
            showDownRes = ShowdownNode::ShowDownResult::TIE;
            logMessage("Showdown! Split pot.");
        }

        std::vector<double> payoffs = showdownNode->get_payoffs(showDownRes, winner);
        double userPayoff = payoffs[activeUserRole];
        
        logMessage(QString("Your Hand: %1 %2 | Opponent Hand: %3 %4")
                   .arg(QString::fromStdString(userCard1.toFormattedString()))
                   .arg(QString::fromStdString(userCard2.toFormattedString()))
                   .arg(QString::fromStdString(oppCard1.toFormattedString()))
                   .arg(QString::fromStdString(oppCard2.toFormattedString())));
        logMessage(QString("Payoff: You %1 BB")
                   .arg(userPayoff >= 0 ? "+" + QString::number(userPayoff, 'f', 2) : QString::number(userPayoff, 'f', 2)));

        ui->nextHandButton->setText("Next Hand ➡️");
        ui->nextHandButton->show();
        clearLayout(ui->actionButtonsLayout);
        return;
    }

    // Handle action nodes
    if (m_currentNode->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
        int playerToAct = actionNode->getPlayer();

        if (actionNode->getActions().size() == 1 && actionNode->getActions()[0].getAction() == GameTreeNode::PokerActions::CHECK) {
            double effStack = qSolverJob->stack - (actionNode->getPot() / 2.0);
            bool isAllIn = (effStack <= 0.01);

            if (!isAllIn) {
                if (playerToAct == activeUserRole) {
                    logMessage("You auto-check.");
                } else {
                    logMessage("Opponent auto-checks.");
                }
                m_currentNode = actionNode->getChildrens()[0];
                QTimer::singleShot(400, this, [this]() {
                    if (!sessionActive) return;
                    processGameState();
                });
            } else {
                m_currentNode = actionNode->getChildrens()[0];
                processGameState();
            }
            return;
        }

        updateRangeViews();

        if (playerToAct == activeUserRole) {
            // User's turn
            logMessage("<b>Your turn to act...</b>");
            if (ui->rangeChallengeCheckBox->isChecked() && ui->rangeChallengeModeComboBox->currentIndex() == 0) {
                displayRangeChallengeSliders();
            } else {
                displayActionButtons();
            }
        } else {
            // Opponent's turn
            logMessage("Opponent is thinking...");
            clearLayout(ui->actionButtonsLayout);

            // Wait 800ms before opponent makes decision
            QTimer::singleShot(800, this, [this, actionNode]() {
                if (!sessionActive) return;
                std::vector<vector<vector<float>>> current_strategy = qSolverJob->get_solver()->get_solver()->get_strategy(actionNode, getChanceCardsAtNode(actionNode));
                int c1 = oppCard1.getCardInt();
                int c2 = oppCard2.getCardInt();
                std::vector<float> oppStrategy;
                if (c1 < (int)current_strategy.size() && c2 < (int)current_strategy[c1].size()) {
                    oppStrategy = current_strategy[c1][c2];
                }
                if (oppStrategy.empty() && c2 < (int)current_strategy.size() && c1 < (int)current_strategy[c2].size()) {
                    oppStrategy = current_strategy[c2][c1];
                }

                if (oppStrategy.empty()) {
                    // Fallback
                    oppStrategy = std::vector<float>(actionNode->getActions().size(), 0.0f);
                    oppStrategy[0] = 1.0f; // Default to Fold/first action
                }

                std::vector<float> adjusted = adjustStrategyForProfile(oppStrategy, actionNode->getActions(), oppProfile);

                // Safety: ensure weights are valid for discrete_distribution
                double adjSum = 0.0;
                for (auto v : adjusted) {
                    if (std::isnan(v) || std::isinf(v)) { adjSum = 0.0; break; }
                    adjSum += v;
                }
                int sampledIdx = 0;
                if (adjSum > 0.0) {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::discrete_distribution<int> dist(adjusted.begin(), adjusted.end());
                    sampledIdx = dist(gen);
                }
                if (sampledIdx < 0 || sampledIdx >= (int)actionNode->getChildrens().size()) {
                    sampledIdx = 0;
                }

                GameActions chosenAction = actionNode->getActions()[sampledIdx];
                QString actName = QString::fromStdString(chosenAction.toString());
                actName.replace("BET_", "Bet ");
                actName.replace("RAISE_", "Raise ");
                actName.replace("CALL", "Call");
                actName.replace("CHECK", "Check");
                actName.replace("FOLD", "Fold");

                auto advanceGame = [this, actionNode, sampledIdx, actName]() {
                    logMessage(QString("Opponent chose: <b>%1</b>").arg(actName));
                    if (sampledIdx >= 0 && sampledIdx < (int)actionNode->getChildrens().size()) {
                        m_currentNode = actionNode->getChildrens()[sampledIdx];
                    }
                    processGameState();
                };

                if (ui->rangeQuizCheckBox->isChecked()) {
                    int opponentPlayer = 1 - activeUserRole;
                    showRangeMatcherQuiz(opponentPlayer, sampledIdx, actionNode, advanceGame);
                } else {
                    advanceGame();
                }
            });
        }
    }
}

void GtoTrainerWindow::displayActionButtons()
{
    clearLayout(ui->actionButtonsLayout);
    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    if (!actionNode) return;

    auto& actions = actionNode->getActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        QString actText = QString::fromStdString(actions[i].toString());
        actText.replace("BET_", "Bet ");
        actText.replace("RAISE_", "Raise ");
        actText.replace("CALL", "Call");
        actText.replace("CHECK", "Check");
        actText.replace("FOLD", "Fold");

        QPushButton* btn = new QPushButton(actText, this);
        btn->setMinimumHeight(40);
        
        // Color coding
        if (actions[i].getAction() == GameTreeNode::PokerActions::FOLD) {
            btn->setStyleSheet("background-color: #d35400; color: white; font-weight: bold; border-radius: 4px;");
        } else if (actions[i].getAction() == GameTreeNode::PokerActions::CHECK || actions[i].getAction() == GameTreeNode::PokerActions::CALL) {
            btn->setStyleSheet("background-color: #27ae60; color: white; font-weight: bold; border-radius: 4px;");
        } else {
            btn->setStyleSheet("background-color: #2980b9; color: white; font-weight: bold; border-radius: 4px;");
        }

        ui->actionButtonsLayout->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onActionButtonClicked(i);
        });
    }
}

void GtoTrainerWindow::onActionButtonClicked(int actionIndex)
{
    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    if (!actionNode) return;

    if (ui->rangeChallengeCheckBox->isChecked() && ui->rangeChallengeModeComboBox->currentIndex() == 1) {
        // Quick/Buttons Range Challenge mode
        if (!qSolverJob || !qSolverJob->get_solver() || !qSolverJob->get_solver()->get_solver()) {
            logMessage("Error: Solver is not initialized or solved.");
            return;
        }

        std::vector<vector<vector<float>>> current_strategy = qSolverJob->get_solver()->get_solver()->get_strategy(actionNode, getChanceCardsAtNode(actionNode));

        std::vector<PrivateCards> userRange = (activeUserRole == 0) ? qSolverJob->get_solver()->player1Range : qSolverJob->get_solver()->player2Range;
        auto& tableRange = (activeUserRole == 0) ? p1TableModel->p1_range : p2TableModel->p2_range;

        auto& actions = actionNode->getActions();
        int numActions = actions.size();
        std::vector<double> gtoSum(numActions, 0.0);
        double totalWeight = 0.0;

        for (auto& pc : userRange) {
            bool blocked = false;
            for (Card& bc : activeBoard) {
                if (bc.getCardInt() == pc.card1 || bc.getCardInt() == pc.card2) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;

            double weight = 0.0;
            if (pc.card1 < (int)tableRange.size() && pc.card2 < (int)tableRange[pc.card1].size()) {
                weight = tableRange[pc.card1][pc.card2];
            }
            if (weight <= 0.0) continue;

            std::vector<float> comboStrat;
            if (pc.card1 < (int)current_strategy.size() && pc.card2 < (int)current_strategy[pc.card1].size()) {
                comboStrat = current_strategy[pc.card1][pc.card2];
            }
            if (comboStrat.empty() && pc.card2 < (int)current_strategy.size() && pc.card1 < (int)current_strategy[pc.card2].size()) {
                comboStrat = current_strategy[pc.card2][pc.card1];
            }
            if (comboStrat.empty()) {
                comboStrat = std::vector<float>(numActions, 0.0f);
                comboStrat[0] = 1.0f;
            }

            for (int a = 0; a < numActions; ++a) {
                float val = (a < (int)comboStrat.size()) ? comboStrat[a] : 0.0f;
                gtoSum[a] += weight * val;
            }
            totalWeight += weight;
        }

        std::vector<double> gtoFrequencies(numActions, 0.0);
        if (totalWeight > 0.0) {
            for (int a = 0; a < numActions; ++a) {
                gtoFrequencies[a] = (a < (int)gtoSum.size()) ? (gtoSum[a] / totalWeight) : 0.0;
            }
        } else {
            if (numActions > 0) {
                gtoFrequencies[0] = 1.0;
            }
        }

        // Determine the dominant GTO action (the one with highest frequency)
        int bestActionIdx = 0;
        double bestActionFreq = -1.0;
        for (int a = 0; a < numActions; ++a) {
            double freq = (a < (int)gtoFrequencies.size()) ? gtoFrequencies[a] : 0.0;
            if (freq > bestActionFreq) {
                bestActionFreq = freq;
                bestActionIdx = a;
            }
        }

        double accuracy = 0.0;
        if (actionIndex == bestActionIdx) {
            accuracy = 100.0; // Chosen the dominant action
        } else if (bestActionFreq > 0.0) {
            double chosenFreq = (actionIndex >= 0 && actionIndex < (int)gtoFrequencies.size()) ? gtoFrequencies[actionIndex] : 0.0;
            accuracy = 100.0 * (chosenFreq / bestActionFreq); // Scored proportionally to how dominant it is
        }
        if (accuracy < 0.0) accuracy = 0.0;
        if (accuracy > 100.0) accuracy = 100.0;

        // Update stats
        rangeChallengeTotal++;
        rangeChallengeAccuracySum += accuracy;
        updateStatsLabel();

        // Log action
        QString actName = "";
        if (actionIndex >= 0 && actionIndex < (int)actions.size()) {
            GameActions chosenAction = actions[actionIndex];
            actName = QString::fromStdString(chosenAction.toString());
        }
        actName.replace("BET_", "Bet ");
        actName.replace("RAISE_", "Raise ");
        actName.replace("CALL", "Call");
        actName.replace("CHECK", "Check");
        actName.replace("FOLD", "Fold");

        double actionGtoFreq = (actionIndex >= 0 && actionIndex < (int)gtoFrequencies.size()) ? gtoFrequencies[actionIndex] : 0.0;
        QString correctIndicator = (accuracy >= 99.9) ? " (Correct ✔️)" : "";
        QString logStr = QString("Quick Range Choice: %1%2 | GTO Freq: %3% | Accuracy: %4%")
            .arg(actName)
            .arg(correctIndicator)
            .arg(QString::number(actionGtoFreq * 100.0, 'f', 1))
            .arg(QString::number(accuracy, 'f', 1));
        logMessage(logStr);

        // Build beautiful HTML table for Quick Choice (no unfair 100% vs mixed comparison)
        QString tableHtml = "<table border='1' cellpadding='5' cellspacing='0' bgcolor='#2c3e50' style='border-collapse: collapse; width: 100%; font-size: 13px; text-align: center; color: white; border-color: #34495e;'>";
        tableHtml += "<tr bgcolor='#34495e' style='font-weight: bold; color: white;'><th>Action</th><th>GTO Frequency</th><th>Your Selection</th><th>GTO Role</th></tr>";
        for (int a = 0; a < numActions; ++a) {
            QString itemText = "";
            if (a < (int)actions.size()) {
                itemText = QString::fromStdString(actions[a].toString());
            }
            itemText.replace("BET_", "Bet ");
            itemText.replace("RAISE_", "Raise ");
            itemText.replace("CALL", "Call");
            itemText.replace("CHECK", "Check");
            itemText.replace("FOLD", "Fold");

            double gtoVal = (a < (int)gtoFrequencies.size()) ? gtoFrequencies[a] * 100.0 : 0.0;
            
            // Your Selection column content and styling
            QString selectionStr = "";
            QString selectionColor = "white";
            if (a == actionIndex) {
                selectionStr = "Selected";
                if (a == bestActionIdx) {
                    selectionColor = "#27ae60"; // green if dominant
                } else if (gtoVal > 5.0) {
                    selectionColor = "#e67e22"; // orange if mixed
                } else {
                    selectionColor = "#c0392b"; // red if low/no freq
                }
            }

            // GTO Role styling
            QString roleStr = "";
            QString roleColor = "white";
            if (a == bestActionIdx && gtoVal > 0.0) {
                roleStr = "Dominant Action 🌟";
                roleColor = "#f1c40f"; // gold
            } else if (gtoVal > 5.0) {
                roleStr = "Mixed Action";
                roleColor = "#3498db"; // light blue
            } else {
                roleStr = "Low/No Freq";
                roleColor = "#95a5a6"; // gray
            }

            tableHtml += QString("<tr>"
                                 "<td style='color: white;'><b>%1</b></td>"
                                 "<td style='color: white;'>%2%</td>"
                                 "<td style='color: %3;'><b>%4</b></td>"
                                 "<td style='color: %5;'><b>%6</b></td>"
                                 "</tr>")
                .arg(itemText)
                .arg(QString::number(gtoVal, 'f', 1))
                .arg(selectionColor)
                .arg(selectionStr)
                .arg(roleColor)
                .arg(roleStr);
        }
        tableHtml += "</table>";

        QString statusText = "";
        if (actionIndex == bestActionIdx) {
            statusText = "<div style='text-align: center; color: #27ae60; font-weight: bold; margin-bottom: 5px;'>✔️ Correct! You selected the dominant GTO action.</div>";
        } else if (accuracy >= 50.0) {
            statusText = "<div style='text-align: center; color: #e67e22; font-weight: bold; margin-bottom: 5px;'>⚠️ Good Choice! You selected a viable mixed GTO action.</div>";
        } else {
            statusText = "<div style='text-align: center; color: #c0392b; font-weight: bold; margin-bottom: 5px;'>❌ Inaccurate! You selected a low/no frequency GTO action.</div>";
        }

        // Set feedback label HTML
        QString feedbackHtml = QString(
            "<div style='text-align: center; font-size: 15px; font-weight: bold; margin-bottom: 8px;'>Accuracy: <font color='%1'>%2%</font></div>"
            "%3"
            "%4"
        )
        .arg(accuracy >= 90.0 ? "#27ae60" : accuracy >= 70.0 ? "#e67e22" : "#c0392b")
        .arg(QString::number(accuracy, 'f', 1))
        .arg(statusText)
        .arg(tableHtml);

        ui->feedbackLabel->setStyleSheet("background-color: #2c3e50; color: white; border: 1px solid #34495e; border-radius: 6px; padding: 10px;");
        ui->feedbackLabel->setText(feedbackHtml);
        ui->feedbackLabel->show();

        clearLayout(ui->actionButtonsLayout);
        if (actionIndex >= 0 && actionIndex < (int)actionNode->getChildrens().size()) {
            m_currentNode = actionNode->getChildrens()[actionIndex];
        }

        ui->nextHandButton->setText("Continue ➡️");
        ui->nextHandButton->show();
        return;
    }

    std::vector<vector<vector<float>>> current_strategy;
    std::vector<vector<vector<float>>> current_evs;
    if (qSolverJob && qSolverJob->get_solver() && qSolverJob->get_solver()->get_solver()) {
        current_strategy = qSolverJob->get_solver()->get_solver()->get_strategy(actionNode, getChanceCardsAtNode(actionNode));
        current_evs = qSolverJob->get_solver()->get_solver()->get_evs(actionNode, getChanceCardsAtNode(actionNode));
    }

    int u1 = userCard1.getCardInt();
    int u2 = userCard2.getCardInt();
    std::vector<float> userStrategy;
    std::vector<float> userEvs;
    if (u1 < (int)current_strategy.size() && u2 < (int)current_strategy[u1].size()) {
        userStrategy = current_strategy[u1][u2];
    }
    if (u1 < (int)current_evs.size() && u2 < (int)current_evs[u1].size()) {
        userEvs = current_evs[u1][u2];
    }
    if (userStrategy.empty() && u2 < (int)current_strategy.size() && u1 < (int)current_strategy[u2].size()) {
        userStrategy = current_strategy[u2][u1];
    }
    if (userEvs.empty() && u2 < (int)current_evs.size() && u1 < (int)current_evs[u2].size()) {
        userEvs = current_evs[u2][u1];
    }

    if (userStrategy.empty()) {
        logMessage("GTO Strategy not found for your hand. Skipping evaluation.");
        if (actionIndex >= 0 && actionIndex < (int)actionNode->getChildrens().size()) {
            m_currentNode = actionNode->getChildrens()[actionIndex];
        }
        processGameState();
        return;
    }

    float preflopCommit = (activeUserRole == 0) ? qSolverJob->ip_commit : qSolverJob->oop_commit;

    float chosenFreq = userStrategy[actionIndex];
    float rawChosenEv = userEvs[actionIndex];
    float chosenEv = rawChosenEv + preflopCommit;

    float rawMaxEv = -999999.0f;
    for (float ev : userEvs) {
        if (ev > rawMaxEv) rawMaxEv = ev;
    }
    float maxEv = rawMaxEv + preflopCommit;

    float evLoss = rawMaxEv - rawChosenEv;
    if (evLoss < 0.0f) evLoss = 0.0f;

    // Display feedback
    displayFeedback(actionIndex, chosenEv, maxEv, evLoss);

    // Update stats
    totalActions++;
    totalEvLoss += evLoss;
    bool isCorrect = (evLoss < 0.02f);
    if (isCorrect) correctActions++;

    updateStatsLabel();

    GameActions chosenAction = actionNode->getActions()[actionIndex];
    QString actName = QString::fromStdString(chosenAction.toString());

    // STANDALONE DB: Log decision
    if (m_dbHandId > 0) {
        int dbSiteId = DBManager::instance().getOrCreateSite("Simulated");
        int dbPlayerId = DBManager::instance().getOrCreatePlayer(dbSiteId, activeUserRole == 0 ? "Hero (IP)" : "Opponent (OOP)");
        
        QString streetStr = "PREFLOP";
        if (m_currentNode->getRound() == GameTreeNode::GameRound::FLOP) streetStr = "FLOP";
        else if (m_currentNode->getRound() == GameTreeNode::GameRound::TURN) streetStr = "TURN";
        else if (m_currentNode->getRound() == GameTreeNode::GameRound::RIVER) streetStr = "RIVER";

        int bestGtoActionIdx = 0;
        float maxGtoFreq = -1.0f;
        for (size_t a = 0; a < userStrategy.size(); ++a) {
            if (userStrategy[a] > maxGtoFreq) {
                maxGtoFreq = userStrategy[a];
                bestGtoActionIdx = a;
            }
        }
        QString bestGtoActionStr = QString::fromStdString(actionNode->getActions()[bestGtoActionIdx].toString());

        DBManager::instance().logGtoDecision(
            m_dbHandId,
            dbPlayerId,
            streetStr,
            "", // action sequence
            actName,
            bestGtoActionStr,
            chosenEv,
            maxEv,
            evLoss
        );
    }
    actName.replace("BET_", "Bet ");
    actName.replace("RAISE_", "Raise ");
    actName.replace("CALL", "Call");
    actName.replace("CHECK", "Check");
    actName.replace("FOLD", "Fold");

    logMessage(QString("You chose: <b>%1</b> (GTO Freq: %2%, EV: %3)")
               .arg(actName)
               .arg(QString::number(chosenFreq * 100.0, 'f', 1))
               .arg(QString::number(chosenEv, 'f', 2)));

    clearLayout(ui->actionButtonsLayout);
    if (actionIndex >= 0 && actionIndex < (int)actionNode->getChildrens().size()) {
        m_currentNode = actionNode->getChildrens()[actionIndex];
    }

    ui->nextHandButton->setText("Continue ➡️");
    ui->nextHandButton->show();
}

void GtoTrainerWindow::onNextHandButtonClicked()
{
    ui->nextHandButton->hide();
    if (ui->nextHandButton->text() == "Continue ➡️") {
        ui->feedbackLabel->clear();
        ui->feedbackLabel->setStyleSheet("");
        ui->feedbackLabel->hide();
        processGameState();
    } else {
        startNewHand();
    }
}

void GtoTrainerWindow::displayFeedback(int chosenActionIdx, float chosenEv, float maxEv, float evLoss)
{
    (void)maxEv;
    QString level;
    QString color;
    if (evLoss < 0.02f) {
        level = "CORRECT";
        color = "#27ae60"; // green
    } else if (evLoss < 0.10f) {
        level = "INACCURACY";
        color = "#e67e22"; // orange
    } else {
        level = "BLUNDER";
        color = "#c0392b"; // red
    }

    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    auto& actions = actionNode->getActions();
    QString chosenActionStr = QString::fromStdString(actions[chosenActionIdx].toString());
    chosenActionStr.replace("BET_", "Bet ");
    chosenActionStr.replace("RAISE_", "Raise ");
    chosenActionStr.replace("CALL", "Call");
    chosenActionStr.replace("CHECK", "Check");
    chosenActionStr.replace("FOLD", "Fold");

    // Find the best GTO action
    int bestActionIdx = 0;
    float bestEv = -999999.0f;
    std::vector<vector<vector<float>>> current_evs = qSolverJob->get_solver()->get_solver()->get_evs(actionNode, getChanceCardsAtNode(actionNode));
    int u1 = userCard1.getCardInt();
    int u2 = userCard2.getCardInt();
    std::vector<float> userEvs = current_evs[u1][u2];
    if (userEvs.empty()) userEvs = current_evs[u2][u1];

    for (size_t i = 0; i < actions.size(); ++i) {
        if (!userEvs.empty() && userEvs[i] > bestEv) {
            bestEv = userEvs[i];
            bestActionIdx = i;
        }
    }

    float preflopCommit = (activeUserRole == 0) ? qSolverJob->ip_commit : qSolverJob->oop_commit;
    if (bestEv > -999998.0f) {
        bestEv += preflopCommit;
    }

    QString bestActionStr = QString::fromStdString(actions[bestActionIdx].toString());
    bestActionStr.replace("BET_", "Bet ");
    bestActionStr.replace("RAISE_", "Raise ");
    bestActionStr.replace("CALL", "Call");
    bestActionStr.replace("CHECK", "Check");
    bestActionStr.replace("FOLD", "Fold");

    QString feedbackHtml = QString(
        "<div style='text-align: center; font-size: 14px;'>\n"
        "<b>%1</b>: You chose %2 (EV: %3). Best GTO action is <b>%4</b> (EV: %5). EV Loss: <b>%6</b> BB\n"
        "</div>"
    )
    .arg(level)
    .arg(chosenActionStr)
    .arg(QString::number(chosenEv, 'f', 2))
    .arg(bestActionStr)
    .arg(QString::number(bestEv, 'f', 2))
    .arg(QString::number(evLoss, 'f', 2));

    ui->feedbackLabel->setStyleSheet(QString("background-color: %1; color: white; border-radius: 6px; padding: 10px;").arg(color));
    ui->feedbackLabel->setText(feedbackHtml);
    ui->feedbackLabel->show();
}

void GtoTrainerWindow::updateRangeViews()
{
    if (p1TableModel) p1TableModel->setGameTreeNode(nullptr);
    if (p2TableModel) p2TableModel->setGameTreeNode(nullptr);
    if (m_currentNodeItem) {
        delete m_currentNodeItem;
        m_currentNodeItem = nullptr;
    }
    m_currentNodeItem = new TreeItem(m_currentNode);

    p1TableModel->setGameTreeNode(m_currentNodeItem);
    p1TableModel->updateStrategyData();
    p2TableModel->setGameTreeNode(m_currentNodeItem);
    p2TableModel->updateStrategyData();

    trueP1Range = p1TableModel->p1_range;
    trueP2Range = p2TableModel->p2_range;

    if (m_streetTransitionOccurred) {
        m_streetStartRangeWeightSum = computeRangeWeightSum(1 - activeUserRole);
        m_streetTransitionOccurred = false;
    }

    int viewModeIdx = ui->viewModeComboBox->currentIndex();
    DetailWindowSetting::DetailWindowMode p1Mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
    DetailWindowSetting::DetailWindowMode p2Mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;

    if (m_currentNode && m_currentNode->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
        int playerToAct = actionNode->getPlayer();

        if (viewModeIdx == 1) {
            if (playerToAct == 0) {
                p1Mode = DetailWindowSetting::DetailWindowMode::STRATEGY;
                p2Mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;
            } else {
                p1Mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
                p2Mode = DetailWindowSetting::DetailWindowMode::STRATEGY;
            }
        } else if (viewModeIdx == 2) {
            if (playerToAct == 0) {
                p1Mode = DetailWindowSetting::DetailWindowMode::EV_ONLY;
                p2Mode = DetailWindowSetting::DetailWindowMode::RANGE_OOP;
            } else {
                p1Mode = DetailWindowSetting::DetailWindowMode::RANGE_IP;
                p2Mode = DetailWindowSetting::DetailWindowMode::EV_ONLY;
            }
        }
    }

    p1Setting.mode = p1Mode;
    p2Setting.mode = p2Mode;

    ui->p1RangeView->viewport()->update();
    ui->p2RangeView->viewport()->update();

    // Rebuild projection buttons
    clearLayout(ui->p1ProjectionsLayout);
    clearLayout(ui->p2ProjectionsLayout);

    if (m_currentNode->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
        int playerToAct = actionNode->getPlayer();
        auto& actions = actionNode->getActions();

        QHBoxLayout* targetLayout = (playerToAct == 0) ? ui->p1ProjectionsLayout : ui->p2ProjectionsLayout;
        QButtonGroup* targetGroup = (playerToAct == 0) ? p1ButtonGroup : p2ButtonGroup;

        // Clear previous buttons from button group
        for (QAbstractButton* btn : targetGroup->buttons()) {
            targetGroup->removeButton(btn);
            delete btn;
        }

        QPushButton* normalBtn = new QPushButton("Normal Range", this);
        normalBtn->setCheckable(true);
        normalBtn->setChecked(true);
        targetLayout->addWidget(normalBtn);
        targetGroup->addButton(normalBtn, -1);

        connect(normalBtn, &QPushButton::clicked, this, [this, playerToAct]() {
            onNormalRangeButtonClicked(playerToAct);
        });

        for (size_t i = 0; i < actions.size(); ++i) {
            QString actName = QString::fromStdString(actions[i].toString());
            actName.replace("BET_", "Bet ");
            actName.replace("RAISE_", "Raise ");
            actName.replace("CALL", "Call");
            actName.replace("CHECK", "Check");
            actName.replace("FOLD", "Fold");

            QPushButton* projBtn = new QPushButton("Project: " + actName, this);
            projBtn->setCheckable(true);
            targetLayout->addWidget(projBtn);
            targetGroup->addButton(projBtn, i);

            connect(projBtn, &QPushButton::clicked, this, [this, playerToAct, i]() {
                onProjectionButtonClicked(playerToAct, i);
            });
        }
    }
}

void GtoTrainerWindow::onNormalRangeButtonClicked(int playerIndex)
{
    if (playerIndex == 0) {
        p1TableModel->p1_range = trueP1Range;
        ui->p1RangeView->viewport()->update();
    } else {
        p2TableModel->p2_range = trueP2Range;
        ui->p2RangeView->viewport()->update();
    }
}

void GtoTrainerWindow::onProjectionButtonClicked(int playerIndex, int actionIndex)
{
    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    if (!actionNode) return;

    std::vector<vector<vector<float>>> current_strategy = qSolverJob->get_solver()->get_solver()->get_strategy(actionNode, getChanceCardsAtNode(actionNode));

    if (playerIndex == 0) {
        p1TableModel->p1_range = trueP1Range;
        float sum = 0.0f;
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                std::vector<float> strat;
                if (i < (int)current_strategy.size() && j < (int)current_strategy[i].size()) {
                    strat = current_strategy[i][j];
                }
                if (strat.empty() && j < (int)current_strategy.size() && i < (int)current_strategy[j].size()) {
                    strat = current_strategy[j][i];
                }
                if (!strat.empty() && actionIndex < (int)strat.size()) {
                    p1TableModel->p1_range[i][j] *= strat[actionIndex];
                } else {
                    p1TableModel->p1_range[i][j] = 0.0f;
                }
                sum += p1TableModel->p1_range[i][j];
            }
        }
        if (sum > 0.0001f) {
            for (int i = 0; i < 52; ++i) {
                for (int j = 0; j < 52; ++j) {
                    p1TableModel->p1_range[i][j] /= sum;
                }
            }
        }
        ui->p1RangeView->viewport()->update();
    } else {
        p2TableModel->p2_range = trueP2Range;
        float sum = 0.0f;
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                std::vector<float> strat;
                if (i < (int)current_strategy.size() && j < (int)current_strategy[i].size()) {
                    strat = current_strategy[i][j];
                }
                if (strat.empty() && j < (int)current_strategy.size() && i < (int)current_strategy[j].size()) {
                    strat = current_strategy[j][i];
                }
                if (!strat.empty() && actionIndex < (int)strat.size()) {
                    p2TableModel->p2_range[i][j] *= strat[actionIndex];
                } else {
                    p2TableModel->p2_range[i][j] = 0.0f;
                }
                sum += p2TableModel->p2_range[i][j];
            }
        }
        if (sum > 0.0001f) {
            for (int i = 0; i < 52; ++i) {
                for (int j = 0; j < 52; ++j) {
                    p2TableModel->p2_range[i][j] /= sum;
                }
            }
        }
        ui->p2RangeView->viewport()->update();
    }
}

void GtoTrainerWindow::logMessage(const QString& msg)
{
    ui->actionLog->append(msg);
    ui->actionLog->moveCursor(QTextCursor::End);
    ui->actionLog->ensureCursorVisible();
}

HandGameState GtoTrainerWindow::buildCurrentGameState()
{
    HandGameState state;
    if (!m_currentNode) return state;

    state.pot = 0.0f;
    state.acting_player_idx = -1;

    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    if (actionNode) {
        state.pot = actionNode->getPot();
        state.acting_player_idx = actionNode->getPlayer();
    } else {
        shared_ptr<TerminalNode> terminalNode = dynamic_pointer_cast<TerminalNode>(m_currentNode);
        if (terminalNode) state.pot = terminalNode->getPot();
        else {
            shared_ptr<ShowdownNode> showdownNode = dynamic_pointer_cast<ShowdownNode>(m_currentNode);
            if (showdownNode) state.pot = showdownNode->getPot();
        }
    }

    std::vector<Card> board = getBoardAtNode(m_currentNode);
    for (const Card& c : board) {
        Card c_copy = c;
        state.board_cards.append(QString::fromStdString(c_copy.toString()));
    }

    if (board.size() == 3) state.current_street = "Flop";
    else if (board.size() == 4) state.current_street = "Turn";
    else if (board.size() == 5) state.current_street = "River";
    else state.current_street = "Preflop";

    if (dynamic_pointer_cast<TerminalNode>(m_currentNode) || dynamic_pointer_cast<ShowdownNode>(m_currentNode)) {
        state.current_street = "Summary";
        state.acting_player_idx = -1;
    }

    PlayerState p1, p2;
    p1.seat = 1;
    p1.name = (activeUserRole == 0) ? "Hero" : "Villain";
    p1.position_label = "IP";
    p1.active = (state.acting_player_idx == 0);
    p1.initial_stack = qSolverJob ? qSolverJob->stack : 100.0f;
    std::pair<double, double> stacks = getPlayerStacks(m_currentNode);
    p1.stack = stacks.first;
    p1.current_bet = 0.0f; // Calculate if needed later

    p2.seat = 2;
    p2.name = (activeUserRole == 1) ? "Hero" : "Villain";
    p2.position_label = "OOP";
    p2.active = (state.acting_player_idx == 1);
    p2.initial_stack = p1.initial_stack;
    p2.stack = stacks.second;
    p2.current_bet = 0.0f;

    if (activeUserRole == 0) {
        Card uc1 = userCard1; Card uc2 = userCard2;
        p1.card1 = QString::fromStdString(uc1.toString());
        p1.card2 = QString::fromStdString(uc2.toString());
    } else {
        Card uc1 = userCard1; Card uc2 = userCard2;
        p2.card1 = QString::fromStdString(uc1.toString());
        p2.card2 = QString::fromStdString(uc2.toString());
    }

    if (state.current_street == "Summary") {
        if (activeUserRole == 0 && oppCard1.getCardInt() != -1) {
            Card oc1 = oppCard1; Card oc2 = oppCard2;
            p2.card1 = QString::fromStdString(oc1.toString());
            p2.card2 = QString::fromStdString(oc2.toString());
        } else if (activeUserRole == 1 && oppCard1.getCardInt() != -1) {
            Card oc1 = oppCard1; Card oc2 = oppCard2;
            p1.card1 = QString::fromStdString(oc1.toString());
            p1.card2 = QString::fromStdString(oc2.toString());
        }
    }

    state.players.push_back(p1);
    state.players.push_back(p2);

    return state;
}

std::vector<Card> GtoTrainerWindow::getBoardAtNode(shared_ptr<GameTreeNode> node)
{
    std::vector<Card> board;
    std::vector<string> board_str_arr = string_split(qSolverJob->board, ',');
    for (string one_board_str : board_str_arr) {
        int card_int = Card::strCard2int(one_board_str);
        auto it = cardint2card.find(card_int);
        if (it != cardint2card.end()) {
            board.push_back(it->second);
        } else {
            board.push_back(Card(one_board_str));
        }
    }
    if (node && qSolverJob && qSolverJob->get_solver() && qSolverJob->get_solver()->getGameTree() && qSolverJob->get_solver()->getGameTree()->getRoot()) {
        GameTreeNode::GameRound root_round = qSolverJob->get_solver()->getGameTree()->getRoot()->getRound();
        GameTreeNode::GameRound current_round = node->getRound();
        if (root_round == GameTreeNode::GameRound::FLOP) {
            if (current_round == GameTreeNode::GameRound::TURN && !turnCard.empty()) {
                auto it = cardint2card.find(turnCard.getCardInt());
                board.push_back(it != cardint2card.end() ? it->second : turnCard);
            }
            if (current_round == GameTreeNode::GameRound::RIVER) {
                if (!turnCard.empty()) {
                    auto it = cardint2card.find(turnCard.getCardInt());
                    board.push_back(it != cardint2card.end() ? it->second : turnCard);
                }
                if (!riverCard.empty()) {
                    auto it = cardint2card.find(riverCard.getCardInt());
                    board.push_back(it != cardint2card.end() ? it->second : riverCard);
                }
            }
        }
        else if (root_round == GameTreeNode::GameRound::TURN) {
            if (current_round == GameTreeNode::GameRound::RIVER && !riverCard.empty()) {
                auto it = cardint2card.find(riverCard.getCardInt());
                board.push_back(it != cardint2card.end() ? it->second : riverCard);
            }
        }
    }
    return board;
}

std::vector<Card> GtoTrainerWindow::getChanceCardsAtNode(shared_ptr<GameTreeNode> node)
{
    std::vector<Card> deal_cards;
    if (!node || !qSolverJob || !qSolverJob->get_solver() || !qSolverJob->get_solver()->getGameTree() || !qSolverJob->get_solver()->getGameTree()->getRoot()) return deal_cards;

    GameTreeNode::GameRound root_round = qSolverJob->get_solver()->getGameTree()->getRoot()->getRound();
    GameTreeNode::GameRound current_round = node->getRound();

    auto lookupCard = [this](Card card) -> Card {
        auto it = cardint2card.find(card.getCardInt());
        if (it != cardint2card.end()) {
            return it->second;
        }
        return card;
    };

    if (root_round == GameTreeNode::GameRound::FLOP) {
        if (current_round == GameTreeNode::GameRound::TURN && !turnCard.empty()) {
            deal_cards.push_back(lookupCard(turnCard));
        }
        if (current_round == GameTreeNode::GameRound::RIVER) {
            if (!turnCard.empty()) deal_cards.push_back(lookupCard(turnCard));
            if (!riverCard.empty()) deal_cards.push_back(lookupCard(riverCard));
        }
    }
    else if (root_round == GameTreeNode::GameRound::TURN) {
        if (current_round == GameTreeNode::GameRound::RIVER && !riverCard.empty()) {
            deal_cards.push_back(lookupCard(riverCard));
        }
    }
    return deal_cards;
}

std::pair<double, double> GtoTrainerWindow::getPlayerStacks(shared_ptr<GameTreeNode> node)
{
    if (!qSolverJob) return {0.0, 0.0};
    shared_ptr<GameTreeNode> root = qSolverJob->get_solver()->getGameTree()->getRoot();

    std::vector<shared_ptr<GameTreeNode>> path;
    shared_ptr<GameTreeNode> curr = node;
    while (curr && curr != root) {
        path.push_back(curr);
        curr = curr->getParent();
    }
    path.push_back(root);
    std::reverse(path.begin(), path.end());

    double ip_commit = qSolverJob->ip_commit;
    double oop_commit = qSolverJob->ip_commit; // at root, both commitments are equal

    for (size_t i = 0; i < path.size() - 1; ++i) {
        shared_ptr<GameTreeNode> parent = path[i];
        shared_ptr<GameTreeNode> child = path[i + 1];

        if (parent->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
            shared_ptr<ActionNode> action_parent = dynamic_pointer_cast<ActionNode>(parent);
            int player = action_parent->getPlayer();

            int action_idx = -1;
            for (size_t idx = 0; idx < action_parent->getChildrens().size(); ++idx) {
                if (action_parent->getChildrens()[idx] == child) {
                    action_idx = idx;
                    break;
                }
            }

            if (action_idx != -1) {
                GameActions act = action_parent->getActions()[action_idx];
                GameTreeNode::PokerActions act_type = act.getAction();

                if (act_type == GameTreeNode::PokerActions::BET || act_type == GameTreeNode::PokerActions::RAISE) {
                    shared_ptr<GameTreeNode> round_root = parent;
                    while (round_root->getParent() && round_root->getParent()->getRound() == parent->getRound()) {
                        round_root = round_root->getParent();
                    }
                    double round_start_pot = round_root->getPot();

                    double new_street_commit = act.getAmount();
                    double new_total_commit = round_start_pot / 2.0 + new_street_commit;

                    if (player == 0) {
                        ip_commit = new_total_commit;
                    } else {
                        oop_commit = new_total_commit;
                    }
                } else if (act_type == GameTreeNode::PokerActions::CALL) {
                    if (player == 0) {
                        ip_commit = oop_commit;
                    } else {
                        oop_commit = ip_commit;
                    }
                }
            }
        }
    }

    double ip_rem = qSolverJob->stack - ip_commit;
    double oop_rem = qSolverJob->stack - oop_commit;
    if (ip_rem < 0.0) ip_rem = 0.0;
    if (oop_rem < 0.0) oop_rem = 0.0;
    return {ip_rem, oop_rem};
}

std::vector<float> GtoTrainerWindow::adjustStrategyForProfile(const std::vector<float>& strategy, const std::vector<GameActions>& actions, OpponentProfile profile)
{
    std::vector<float> adjusted = strategy;
    if (adjusted.empty()) return adjusted;

    std::vector<int> foldIdxs, callCheckIdxs, raiseBetIdxs;
    for (size_t i = 0; i < actions.size(); ++i) {
        GameActions actCopy = actions[i];
        GameTreeNode::PokerActions act = actCopy.getAction();
        if (act == GameTreeNode::PokerActions::FOLD) {
            foldIdxs.push_back(i);
        } else if (act == GameTreeNode::PokerActions::CHECK || act == GameTreeNode::PokerActions::CALL) {
            callCheckIdxs.push_back(i);
        } else if (act == GameTreeNode::PokerActions::BET || act == GameTreeNode::PokerActions::RAISE) {
            raiseBetIdxs.push_back(i);
        }
    }

    if (profile == OpponentProfile::CALLING_STATION) {
        float shiftAmount = 0.0f;
        for (int idx : foldIdxs) {
            float amt = adjusted[idx] * 0.40f;
            adjusted[idx] -= amt;
            shiftAmount += amt;
        }
        for (int idx : raiseBetIdxs) {
            float amt = adjusted[idx] * 0.40f;
            adjusted[idx] -= amt;
            shiftAmount += amt;
        }
        if (!callCheckIdxs.empty()) {
            for (int idx : callCheckIdxs) {
                adjusted[idx] += shiftAmount / callCheckIdxs.size();
            }
        }
    } else if (profile == OpponentProfile::NIT) {
        float raiseToCallShift = 0.0f;
        for (int idx : raiseBetIdxs) {
            float amt = adjusted[idx] * 0.40f;
            adjusted[idx] -= amt;
            raiseToCallShift += amt;
        }
        if (!callCheckIdxs.empty()) {
            for (int idx : callCheckIdxs) {
                adjusted[idx] += raiseToCallShift / callCheckIdxs.size();
            }
        }
        float callToFoldShift = 0.0f;
        for (int idx : callCheckIdxs) {
            float amt = adjusted[idx] * 0.40f;
            adjusted[idx] -= amt;
            callToFoldShift += amt;
        }
        if (!foldIdxs.empty()) {
            for (int idx : foldIdxs) {
                adjusted[idx] += callToFoldShift / foldIdxs.size();
            }
        }
    } else if (profile == OpponentProfile::MANIAC) {
        float shiftAmount = 0.0f;
        for (int idx : foldIdxs) {
            float amt = adjusted[idx] * 0.40f;
            adjusted[idx] -= amt;
            shiftAmount += amt;
        }
        for (int idx : callCheckIdxs) {
            float amt = adjusted[idx] * 0.40f;
            adjusted[idx] -= amt;
            shiftAmount += amt;
        }
        if (!raiseBetIdxs.empty()) {
            for (int idx : raiseBetIdxs) {
                adjusted[idx] += shiftAmount / raiseBetIdxs.size();
            }
        }
    }

    float sum = 0.0f;
    for (float p : adjusted) sum += p;
    if (sum > 0.0001f) {
        for (float& p : adjusted) p /= sum;
    }
    return adjusted;
}

void GtoTrainerWindow::clearLayout(QLayout* layout)
{
    if (!layout) return;
    QLayoutItem* item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        } else if (item->layout()) {
            clearLayout(item->layout());
        }
        delete item;
    }
}

std::vector<std::vector<float>> GtoTrainerWindow::computeProjectedRange(int playerIdx, int actionIdx, shared_ptr<ActionNode> node, const std::vector<std::vector<float>>& baseRange)
{
    std::vector<std::vector<float>> projectedRange(52, std::vector<float>(52, 0.0f));
    if (!node || !qSolverJob || !qSolverJob->get_solver() || !qSolverJob->get_solver()->get_solver()) {
        return projectedRange;
    }

    std::vector<vector<vector<float>>> strategy = qSolverJob->get_solver()->get_solver()->get_strategy(node, getChanceCardsAtNode(node));

    float sum = 0.0f;
    for (int i = 0; i < 52; ++i) {
        for (int j = 0; j < 52; ++j) {
            float baseW = baseRange[i][j];
            if (baseW <= 0.0f) continue;

            auto strat = strategy[i][j];
            if (strat.empty()) strat = strategy[j][i];

            float actionProb = 0.0f;
            if (!strat.empty() && actionIdx < (int)strat.size()) {
                actionProb = strat[actionIdx];
            }

            float newW = baseW * actionProb;
            projectedRange[i][j] = newW;
            sum += newW;
        }
    }

    if (sum > 1e-5f) {
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                projectedRange[i][j] /= sum;
            }
        }
    }

    return projectedRange;
}

std::vector<std::vector<float>> GtoTrainerWindow::generateStrategicMistakeRange(const std::vector<std::vector<float>>& correctRange, const std::vector<std::vector<float>>& baseRange, std::mt19937& gen)
{
    std::vector<std::vector<float>> mistakeRange(52, std::vector<float>(52, 0.0f));
    
    std::uniform_int_distribution<int> dist(0, 2);
    int mistakeType = dist(gen);
    
    float sum = 0.0f;

    if (mistakeType == 0) {
        // Pure Strategy Simplification (No Mix Mistake)
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                if (baseRange[i][j] > 0.0f) {
                    float freq = correctRange[i][j] / baseRange[i][j];
                    if (freq > 0.5f) {
                        mistakeRange[i][j] = baseRange[i][j];
                    } else {
                        mistakeRange[i][j] = 0.0f;
                    }
                }
            }
        }
    } else if (mistakeType == 1) {
        // Inverted Mix Mistake
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                if (baseRange[i][j] > 0.0f) {
                    float freq = correctRange[i][j] / baseRange[i][j];
                    if (freq > 0.05f && freq < 0.95f) {
                        float newFreq = 1.0f - freq;
                        mistakeRange[i][j] = baseRange[i][j] * newFreq;
                    } else {
                        mistakeRange[i][j] = correctRange[i][j];
                    }
                }
            }
        }
    } else {
        // Suit Blindness Mistake
        std::map<std::pair<int, int>, float> freqSum;
        std::map<std::pair<int, int>, int> freqCount;
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                if (baseRange[i][j] > 0.0f) {
                    float freq = correctRange[i][j] / baseRange[i][j];
                    auto rankPair = std::make_pair(i / 4, j / 4);
                    freqSum[rankPair] += freq;
                    freqCount[rankPair]++;
                }
            }
        }
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                if (baseRange[i][j] > 0.0f) {
                    auto rankPair = std::make_pair(i / 4, j / 4);
                    float avgFreq = freqSum[rankPair] / static_cast<float>(freqCount[rankPair]);
                    mistakeRange[i][j] = baseRange[i][j] * avgFreq;
                }
            }
        }
    }

    // Normalize
    for (int i = 0; i < 52; ++i) {
        for (int j = 0; j < 52; ++j) {
            sum += mistakeRange[i][j];
        }
    }
    
    if (sum > 1e-5f) {
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                mistakeRange[i][j] /= sum;
            }
        }
    } else {
        // Fallback if the mistake somehow wiped the range completely
        return correctRange;
    }

    return mistakeRange;
}

float GtoTrainerWindow::computeRangeWeightSum(int playerIdx)
{
    float sum = 0.0f;
    if (!qSolverJob || !qSolverJob->get_solver()) return sum;

    std::vector<PrivateCards>& range = (playerIdx == 0) ? qSolverJob->get_solver()->player1Range : qSolverJob->get_solver()->player2Range;
    auto& tableRange = (playerIdx == 0) ? p1TableModel->p1_range : p2TableModel->p2_range;

    for (auto& pc : range) {
        bool blocked = false;
        for (Card& bc : activeBoard) {
            if (bc.getCardInt() == pc.card1 || bc.getCardInt() == pc.card2) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            if (pc.card1 < (int)tableRange.size() && pc.card2 < (int)tableRange[pc.card1].size()) {
                sum += tableRange[pc.card1][pc.card2];
            }
        }
    }
    return sum;
}

void GtoTrainerWindow::showRangeMatcherQuiz(int opponentPlayer, int chosenActionIdx, shared_ptr<ActionNode> node, std::function<void()> onComplete)
{
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Range Matcher Quiz");
    dialog->setMinimumSize(1000, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(dialog);

    GameActions chosenAction = node->getActions()[chosenActionIdx];
    QString actName;
    if (chosenAction.getAction() == GameTreeNode::PokerActions::CHECK) {
        actName = "Check";
    } else if (chosenAction.getAction() == GameTreeNode::PokerActions::FOLD) {
        actName = "Fold";
    } else if (chosenAction.getAction() == GameTreeNode::PokerActions::CALL) {
        actName = "Call";
    } else {
        QString actionTypeStr = (chosenAction.getAction() == GameTreeNode::PokerActions::BET) ? "Bet" : "Raise";
        double amt = chosenAction.getAmount();
        QString amtStr;
        if (amt == static_cast<int>(amt)) {
            amtStr = QString::number(static_cast<int>(amt));
        } else {
            amtStr = QString::number(amt, 'f', 1);
        }
        actName = QString("%1 %2").arg(actionTypeStr, amtStr);
    }

    QLabel* headerLabel = new QLabel(QString("<h3>Opponent took action: <b>%1</b></h3><p>Which range heatmap corresponds to their GTO range for this action?</p>").arg(actName), dialog);
    headerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(headerLabel);

    std::vector<std::vector<float>> baseRange = (opponentPlayer == 0) ? trueP1Range : trueP2Range;
    std::vector<std::vector<float>> correctRange = computeProjectedRange(opponentPlayer, chosenActionIdx, node, baseRange);

    int decoyActionIdx = -1;
    float maxDist = -1.0f;
    auto getVal = [](GameActions a) -> float {
        switch(a.getAction()) {
            case GameTreeNode::PokerActions::FOLD:
                return -1.0f;
            case GameTreeNode::PokerActions::CHECK:
                return 0.0f;
            case GameTreeNode::PokerActions::CALL:
                return 0.5f;
            case GameTreeNode::PokerActions::BET:
            case GameTreeNode::PokerActions::RAISE:
                return 2.0f + static_cast<float>(a.getAmount());
            default:
                return 0.0f;
        }
    };

    auto& actions = node->getActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        if ((int)i == chosenActionIdx) continue;
        float dist = std::abs(getVal(actions[i]) - getVal(actions[chosenActionIdx]));
        if (dist > maxDist) {
            maxDist = dist;
            decoyActionIdx = i;
        }
    }

    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<std::vector<float>> decoyRange;
    bool useDecoy = false;
    if (decoyActionIdx != -1) {
        decoyRange = computeProjectedRange(opponentPlayer, decoyActionIdx, node, baseRange);
        float sum = 0.0f;
        for (int i = 0; i < 52; ++i) {
            for (int j = 0; j < 52; ++j) {
                sum += decoyRange[i][j];
            }
        }
        if (sum > 0.01f) {
            // Check if decoyRange is sufficiently distinct from correctRange (L1 distance >= 0.4)
            float dist = 0.0f;
            for (int i = 0; i < 52; ++i) {
                for (int j = 0; j < 52; ++j) {
                    dist += std::abs(correctRange[i][j] - decoyRange[i][j]);
                }
            }
            if (dist >= 0.4f) {
                useDecoy = true;
            }
        }
    }
    if (!useDecoy) {
        decoyRange = generateStrategicMistakeRange(correctRange, baseRange, gen);
    }

    struct OptionData {
        std::vector<std::vector<float>> range;
        bool isCorrect;
    };

    std::vector<OptionData> optionsList = {
        { correctRange, true },
        { decoyRange, false }
    };

    std::shuffle(optionsList.begin(), optionsList.end(), gen);

    QHBoxLayout* rangesLayout = new QHBoxLayout();
    mainLayout->addLayout(rangesLayout);

    std::vector<QPushButton*> selectButtons;

    for (size_t i = 0; i < optionsList.size(); ++i) {
        QVBoxLayout* colLayout = new QVBoxLayout();

        QLabel* optionLabel = new QLabel(QString("Option %1").arg(QChar('A' + (int)i)), dialog);
        optionLabel->setAlignment(Qt::AlignCenter);
        optionLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
        colLayout->addWidget(optionLabel);

        HtmlTableView* view = new HtmlTableView(dialog);
        view->setMinimumSize(280, 280);
        view->setMaximumSize(320, 320);
        view->horizontalHeader()->setVisible(false);
        view->verticalHeader()->setVisible(false);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setFrameShape(QFrame::NoFrame);

        TableStrategyModel* model = new TableStrategyModel(qSolverJob, view);
        DetailWindowSetting* setting = new DetailWindowSetting();
        setting->mode = (opponentPlayer == 0) ? DetailWindowSetting::RANGE_IP : DetailWindowSetting::RANGE_OOP;

        StrategyItemDelegate* delegate = new StrategyItemDelegate(qSolverJob, setting, view, 7);

        if (opponentPlayer == 0) {
            model->p1_range = optionsList[i].range;
        } else {
            model->p2_range = optionsList[i].range;
        }

        view->setModel(model);
        view->setItemDelegate(delegate);
        view->setAutoSizeRows(true);
        view->setMinRowHeight(21);

        colLayout->addWidget(view);

        QPushButton* btn = new QPushButton(QString("Select Option %1").arg(QChar('A' + (int)i)), dialog);
        btn->setMinimumHeight(40);
        btn->setStyleSheet("QPushButton { font-weight: bold; font-size: 13px; border-radius: 6px; padding: 6px; background-color: #2980b9; color: white; }"
                           "QPushButton:hover { background-color: #3498db; }");
        colLayout->addWidget(btn);

        rangesLayout->addLayout(colLayout);
        selectButtons.push_back(btn);
    }

    QLabel* feedbackLabel = new QLabel(dialog);
    feedbackLabel->setAlignment(Qt::AlignCenter);
    feedbackLabel->setStyleSheet("font-size: 15px; font-weight: bold; margin: 10px;");
    mainLayout->addWidget(feedbackLabel);

    QPushButton* continueBtn = new QPushButton("Continue", dialog);
    continueBtn->setMinimumHeight(45);
    continueBtn->setMaximumWidth(200);
    continueBtn->setStyleSheet("QPushButton { font-weight: bold; font-size: 14px; border-radius: 6px; padding: 8px; background-color: #27ae60; color: white; }"
                               "QPushButton:hover { background-color: #2ecc71; }");
    continueBtn->setEnabled(false);
    mainLayout->addWidget(continueBtn, 0, Qt::AlignCenter);

    for (size_t i = 0; i < selectButtons.size(); ++i) {
        connect(selectButtons[i], &QPushButton::clicked, dialog, [this, i, optionsList, selectButtons, feedbackLabel, continueBtn]() {
            for (auto* b : selectButtons) {
                b->setEnabled(false);
            }

            this->m_quizTotal++;
            bool correctSelected = optionsList[i].isCorrect;
            if (correctSelected) {
                this->m_quizCorrect++;
                selectButtons[i]->setStyleSheet("background-color: #27ae60; color: white; font-weight: bold; font-size: 13px; border-radius: 6px; padding: 6px;");
                feedbackLabel->setText("<font color='#27ae60'>Correct! You identified the true GTO range.</font>");
            } else {
                selectButtons[i]->setStyleSheet("background-color: #c0392b; color: white; font-weight: bold; font-size: 13px; border-radius: 6px; padding: 6px;");
                for (size_t j = 0; j < optionsList.size(); ++j) {
                    if (optionsList[j].isCorrect) {
                        selectButtons[j]->setStyleSheet("background-color: #27ae60; color: white; font-weight: bold; font-size: 13px; border-radius: 6px; padding: 6px;");
                        feedbackLabel->setText(QString("<font color='#c0392b'>Incorrect. The correct GTO range is Option %1.</font>").arg(QChar('A' + (int)j)));
                        break;
                    }
                }
            }

            this->updateStatsLabel();

            continueBtn->setEnabled(true);
        });
    }

    connect(continueBtn, &QPushButton::clicked, dialog, &QDialog::accept);

    dialog->exec();
    delete dialog;

    onComplete();
}

void GtoTrainerWindow::showNarrowingChallenge(int opponentPlayer, std::function<void()> onComplete)
{
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Range Narrowing Challenge");
    dialog->setMinimumSize(500, 350);

    QVBoxLayout* mainLayout = new QVBoxLayout(dialog);

    QLabel* headerLabel = new QLabel(dialog);
    headerLabel->setText("<h3>Range Narrowing Challenge</h3><p>What percentage of the opponent's starting street range survived to the next street?</p>");
    headerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(headerLabel);

    float ratio = 1.0f;
    float currentWeightSum = 0.0f;
    if (m_streetStartRangeWeightSum > 0.0001f) {
        currentWeightSum = computeRangeWeightSum(opponentPlayer);
        ratio = currentWeightSum / m_streetStartRangeWeightSum;
    }
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    int correctBucketIdx = 0;
    if (ratio < 0.25f) {
        correctBucketIdx = 0;
    } else if (ratio < 0.50f) {
        correctBucketIdx = 1;
    } else if (ratio < 0.75f) {
        correctBucketIdx = 2;
    } else {
        correctBucketIdx = 3;
    }

    std::vector<QString> bucketTexts = {
        "Less than 25%",
        "25% - 50%",
        "50% - 75%",
        "More than 75%"
    };

    std::vector<QPushButton*> buttons;
    QVBoxLayout* btnLayout = new QVBoxLayout();
    for (int i = 0; i < 4; ++i) {
        QPushButton* btn = new QPushButton(bucketTexts[i], dialog);
        btn->setMinimumHeight(45);
        btn->setStyleSheet("QPushButton { font-weight: bold; font-size: 14px; border-radius: 6px; padding: 8px; background-color: #2980b9; color: white; }"
                           "QPushButton:hover { background-color: #3498db; }");
        btnLayout->addWidget(btn);
        buttons.push_back(btn);
    }
    mainLayout->addLayout(btnLayout);

    QLabel* feedbackLabel = new QLabel(dialog);
    feedbackLabel->setAlignment(Qt::AlignCenter);
    feedbackLabel->setStyleSheet("font-size: 15px; font-weight: bold; margin: 10px;");
    mainLayout->addWidget(feedbackLabel);

    QPushButton* continueBtn = new QPushButton("Continue", dialog);
    continueBtn->setMinimumHeight(45);
    continueBtn->setMaximumWidth(150);
    continueBtn->setStyleSheet("QPushButton { font-weight: bold; font-size: 14px; border-radius: 6px; padding: 8px; background-color: #27ae60; color: white; }"
                               "QPushButton:hover { background-color: #2ecc71; }");
    continueBtn->setEnabled(false);
    mainLayout->addWidget(continueBtn, 0, Qt::AlignCenter);

    for (int i = 0; i < 4; ++i) {
        connect(buttons[i], &QPushButton::clicked, dialog, [this, i, correctBucketIdx, ratio, bucketTexts, buttons, feedbackLabel, continueBtn]() {
            for (auto* btn : buttons) {
                btn->setEnabled(false);
            }

            this->m_narrowingTotal++;
            bool isCorrect = (i == correctBucketIdx);
            if (isCorrect) {
                this->m_narrowingCorrect++;
                buttons[i]->setStyleSheet("background-color: #27ae60; color: white; font-weight: bold; font-size: 14px; border-radius: 6px; padding: 8px;");
                feedbackLabel->setText(QString("<font color='#27ae60'>Correct! Survived range: %1%</font>")
                                       .arg(QString::number(ratio * 100.0f, 'f', 1)));
            } else {
                buttons[i]->setStyleSheet("background-color: #c0392b; color: white; font-weight: bold; font-size: 14px; border-radius: 6px; padding: 8px;");
                buttons[correctBucketIdx]->setStyleSheet("background-color: #27ae60; color: white; font-weight: bold; font-size: 14px; border-radius: 6px; padding: 8px;");
                feedbackLabel->setText(QString("<font color='#c0392b'>Incorrect. The correct range is %1 (Actual: %2%)</font>")
                                       .arg(bucketTexts[correctBucketIdx])
                                       .arg(QString::number(ratio * 100.0f, 'f', 1)));
            }

            this->updateStatsLabel();

            continueBtn->setEnabled(true);
        });
    }

    connect(continueBtn, &QPushButton::clicked, dialog, &QDialog::accept);

    dialog->exec();
    delete dialog;

    onComplete();
}

void GtoTrainerWindow::displayRangeChallengeSliders()
{
    clearLayout(ui->actionButtonsLayout);
    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    if (!actionNode) return;

    auto& actions = actionNode->getActions();
    int numActions = actions.size();
    if (numActions == 0) return;

    m_rangeSliders.clear();
    m_rangeSliderLabels.clear();
    m_sliderValues.clear();
    m_sliderValues.resize(numActions, 0);

    // Initial equal distribution summing to 100%
    int baseVal = 100 / numActions;
    int rem = 100 % numActions;
    for (int i = 0; i < numActions; ++i) {
        m_sliderValues[i] = baseVal + (i < rem ? 1 : 0);
    }

    QVBoxLayout* mainSlidersLayout = new QVBoxLayout();
    mainSlidersLayout->setSpacing(10);

    for (int i = 0; i < numActions; ++i) {
        QHBoxLayout* rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(10);

        QString actText = QString::fromStdString(actions[i].toString());
        actText.replace("BET_", "Bet ");
        actText.replace("RAISE_", "Raise ");
        actText.replace("CALL", "Call");
        actText.replace("CHECK", "Check");
        actText.replace("FOLD", "Fold");

        QLabel* nameLabel = new QLabel(actText, this);
        nameLabel->setMinimumWidth(90);

        // Premium styling matching action type
        QString colorHex = "#2980b9"; // default blue
        if (actions[i].getAction() == GameTreeNode::PokerActions::FOLD) {
            colorHex = "#d35400"; // orange-red
        } else if (actions[i].getAction() == GameTreeNode::PokerActions::CHECK || actions[i].getAction() == GameTreeNode::PokerActions::CALL) {
            colorHex = "#27ae60"; // green
        }
        nameLabel->setStyleSheet(QString("font-weight: bold; font-size: 13px; color: %1;").arg(colorHex));

        QSlider* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, 100);
        slider->setValue(m_sliderValues[i]);
        slider->setMinimumWidth(200);

        // Apply a premium dark slider theme
        slider->setStyleSheet(QString(
            "QSlider::groove:horizontal {"
            "    border: 1px solid #34495e;"
            "    height: 8px;"
            "    background: #2c3e50;"
            "    margin: 2px 0;"
            "    border-radius: 4px;"
            "}"
            "QSlider::handle:horizontal {"
            "    background: %1;"
            "    border: 1px solid #1abc9c;"
            "    width: 18px;"
            "    height: 18px;"
            "    margin: -6px 0;"
            "    border-radius: 9px;"
            "}"
            "QSlider::handle:horizontal:hover {"
            "    background: #1abc9c;"
            "}"
        ).arg(colorHex));

        QLabel* pctLabel = new QLabel(QString("%1%").arg(m_sliderValues[i]), this);
        pctLabel->setMinimumWidth(40);
        pctLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pctLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #ffffff;");

        rowLayout->addWidget(nameLabel);
        rowLayout->addWidget(slider);
        rowLayout->addWidget(pctLabel);

        mainSlidersLayout->addLayout(rowLayout);

        m_rangeSliders.push_back(slider);
        m_rangeSliderLabels.push_back(pctLabel);

        connect(slider, &QSlider::valueChanged, this, [this, i](int val) {
            onSliderValueChanged(i, val);
        });
    }

    // Calculate initial sum
    int initialSum = 0;
    for (int val : m_sliderValues) {
        initialSum += val;
    }

    // Sum Label
    m_sliderSumLabel = new QLabel(QString("Total: %1%").arg(initialSum), this);
    m_sliderSumLabel->setAlignment(Qt::AlignCenter);
    m_sliderSumLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2cc7c9;");
    mainSlidersLayout->addWidget(m_sliderSumLabel);

    // Precise Slider adjustments instruction/tip label
    QLabel* tipLabel = new QLabel("💡 <i>Tip: Use mouse wheel or keyboard arrow keys to adjust sliders by 1% precisely.<br>Drag freely to set relative weights; the system auto-normalizes to 100% on submit!</i>", this);
    tipLabel->setAlignment(Qt::AlignCenter);
    tipLabel->setStyleSheet("font-size: 11px; color: #bdc3c7; margin-top: 2px; margin-bottom: 5px;");
    mainSlidersLayout->addWidget(tipLabel);

    // Horizontal layout for buttons (Normalize + Submit)
    QHBoxLayout* btnRowLayout = new QHBoxLayout();
    btnRowLayout->setSpacing(10);

    QPushButton* normalizeBtn = new QPushButton("Normalize to 100% ⚖️", this);
    normalizeBtn->setMinimumHeight(40);
    normalizeBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #2980b9;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 14px;"
        "    border-radius: 6px;"
        "    padding: 8px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #3498db;"
        "}"
    );
    btnRowLayout->addWidget(normalizeBtn);

    connect(normalizeBtn, &QPushButton::clicked, this, [this]() {
        int numSliders = m_rangeSliders.size();
        if (numSliders <= 1) return;

        int currentSum = 0;
        for (int v : m_sliderValues) {
            currentSum += v;
        }

        m_updatingSliders = true;
        if (currentSum > 0) {
            double ratio = 100.0 / currentSum;
            int newSum = 0;
            for (int i = 0; i < numSliders; ++i) {
                m_sliderValues[i] = std::round(m_sliderValues[i] * ratio);
                newSum += m_sliderValues[i];
            }
            int diff = 100 - newSum;
            if (diff != 0) {
                int largestIdx = 0;
                int largestVal = -1;
                for (int i = 0; i < numSliders; ++i) {
                    if (m_sliderValues[i] > largestVal) {
                        largestVal = m_sliderValues[i];
                        largestIdx = i;
                    }
                }
                m_sliderValues[largestIdx] += diff;
                if (m_sliderValues[largestIdx] < 0) m_sliderValues[largestIdx] = 0;
            }
        } else {
            int baseVal = 100 / numSliders;
            int rem = 100 % numSliders;
            for (int i = 0; i < numSliders; ++i) {
                m_sliderValues[i] = baseVal + (i < rem ? 1 : 0);
            }
        }

        for (int i = 0; i < numSliders; ++i) {
            m_rangeSliders[i]->blockSignals(true);
            m_rangeSliders[i]->setValue(m_sliderValues[i]);
            m_rangeSliders[i]->blockSignals(false);
            m_rangeSliderLabels[i]->setText(QString("%1%").arg(m_sliderValues[i]));
        }

        m_sliderSumLabel->setText("Total: 100%");
        m_sliderSumLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2cc7c9;");
        m_updatingSliders = false;
    });

    // Submit button
    QPushButton* submitBtn = new QPushButton("Submit Strategy Check ✔️", this);
    submitBtn->setMinimumHeight(40);
    submitBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #27ae60;"
        "    color: white;"
        "    font-weight: bold;"
        "    font-size: 14px;"
        "    border-radius: 6px;"
        "    padding: 8px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #2ecc71;"
        "}"
    );
    btnRowLayout->addWidget(submitBtn);

    mainSlidersLayout->addLayout(btnRowLayout);

    connect(submitBtn, &QPushButton::clicked, this, &GtoTrainerWindow::onSubmitRangeStrategy);

    ui->actionButtonsLayout->addLayout(mainSlidersLayout);
}

void GtoTrainerWindow::onSliderValueChanged(int sliderIdx, int value)
{
    if (m_updatingSliders) return;
    m_updatingSliders = true;

    // Clamp value
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    m_sliderValues[sliderIdx] = value;
    m_rangeSliders[sliderIdx]->setValue(value);
    m_rangeSliderLabels[sliderIdx]->setText(QString("%1%").arg(value));

    // Calculate sum of all sliders
    int totalSum = 0;
    for (int v : m_sliderValues) {
        totalSum += v;
    }

    // Update the sum label
    if (m_sliderSumLabel) {
        m_sliderSumLabel->setText(QString("Total: %1%").arg(totalSum));
        if (totalSum == 100) {
            m_sliderSumLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2cc7c9;");
        } else {
            m_sliderSumLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #e74c3c;");
        }
    }

    m_updatingSliders = false;
}

void GtoTrainerWindow::onSubmitRangeStrategy()
{
    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(m_currentNode);
    if (!actionNode) return;

    if (!qSolverJob || !qSolverJob->get_solver() || !qSolverJob->get_solver()->get_solver()) {
        logMessage("Error: Solver is not initialized or solved.");
        return;
    }

    // Auto-normalize if sum is not 100%
    int currentSum = 0;
    for (int v : m_sliderValues) {
        currentSum += v;
    }
    if (currentSum != 100) {
        int numSliders = m_sliderValues.size();
        if (numSliders > 1) {
            if (currentSum > 0) {
                double ratio = 100.0 / currentSum;
                int newSum = 0;
                for (int i = 0; i < numSliders; ++i) {
                    m_sliderValues[i] = std::round(m_sliderValues[i] * ratio);
                    newSum += m_sliderValues[i];
                }
                int diff = 100 - newSum;
                if (diff != 0) {
                    int largestIdx = 0;
                    int largestVal = -1;
                    for (int i = 0; i < numSliders; ++i) {
                        if (m_sliderValues[i] > largestVal) {
                            largestVal = m_sliderValues[i];
                            largestIdx = i;
                        }
                    }
                    m_sliderValues[largestIdx] += diff;
                    if (m_sliderValues[largestIdx] < 0) m_sliderValues[largestIdx] = 0;
                }
            } else {
                int baseVal = 100 / numSliders;
                int rem = 100 % numSliders;
                for (int i = 0; i < numSliders; ++i) {
                    m_sliderValues[i] = baseVal + (i < rem ? 1 : 0);
                }
            }
        }
    }

    std::vector<vector<vector<float>>> current_strategy = qSolverJob->get_solver()->get_solver()->get_strategy(actionNode, getChanceCardsAtNode(actionNode));

    std::vector<PrivateCards> userRange = (activeUserRole == 0) ? qSolverJob->get_solver()->player1Range : qSolverJob->get_solver()->player2Range;
    auto& tableRange = (activeUserRole == 0) ? p1TableModel->p1_range : p2TableModel->p2_range;

    auto& actions = actionNode->getActions();
    int numActions = actions.size();
    std::vector<double> gtoSum(numActions, 0.0);
    double totalWeight = 0.0;

    for (auto& pc : userRange) {
        bool blocked = false;
        for (Card& bc : activeBoard) {
            if (bc.getCardInt() == pc.card1 || bc.getCardInt() == pc.card2) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;

        double weight = 0.0;
        if (pc.card1 < (int)tableRange.size() && pc.card2 < (int)tableRange[pc.card1].size()) {
            weight = tableRange[pc.card1][pc.card2];
        }
        if (weight <= 0.0) continue;

        std::vector<float> comboStrat;
        if (pc.card1 < (int)current_strategy.size() && pc.card2 < (int)current_strategy[pc.card1].size()) {
            comboStrat = current_strategy[pc.card1][pc.card2];
        }
        if (comboStrat.empty() && pc.card2 < (int)current_strategy.size() && pc.card1 < (int)current_strategy[pc.card2].size()) {
            comboStrat = current_strategy[pc.card2][pc.card1];
        }
        if (comboStrat.empty()) {
            comboStrat = std::vector<float>(numActions, 0.0f);
            comboStrat[0] = 1.0f;
        }

        for (int a = 0; a < numActions; ++a) {
            float val = (a < (int)comboStrat.size()) ? comboStrat[a] : 0.0f;
            gtoSum[a] += weight * val;
        }
        totalWeight += weight;
    }

    std::vector<double> gtoFrequencies(numActions, 0.0);
    if (totalWeight > 0.0) {
        for (int a = 0; a < numActions; ++a) {
            gtoFrequencies[a] = (a < (int)gtoSum.size()) ? (gtoSum[a] / totalWeight) : 0.0;
        }
    } else {
        if (numActions > 0) {
            gtoFrequencies[0] = 1.0;
        }
    }

    double sumAbsDiff = 0.0;
    for (int a = 0; a < numActions; ++a) {
        double guessVal = (a < (int)m_sliderValues.size()) ? ((double)m_sliderValues[a] / 100.0) : 0.0;
        double gtoVal = (a < (int)gtoFrequencies.size()) ? gtoFrequencies[a] : 0.0;
        sumAbsDiff += std::abs(guessVal - gtoVal);
    }
    double accuracy = 100.0 * (1.0 - 0.5 * sumAbsDiff);
    if (accuracy < 0.0) accuracy = 0.0;
    if (accuracy > 100.0) accuracy = 100.0;

    // Update stats
    rangeChallengeTotal++;
    rangeChallengeAccuracySum += accuracy;
    updateStatsLabel();

    // Log the user's action
    QString logStr = "Range Strategy Submitted: ";
    for (int a = 0; a < numActions; ++a) {
        QString actText = "";
        if (a < (int)actions.size()) {
            actText = QString::fromStdString(actions[a].toString());
        }
        actText.replace("BET_", "Bet ");
        actText.replace("RAISE_", "Raise ");
        actText.replace("CALL", "Call");
        actText.replace("CHECK", "Check");
        actText.replace("FOLD", "Fold");

        int guessVal = (a < (int)m_sliderValues.size()) ? m_sliderValues[a] : 0;
        double gtoVal = (a < (int)gtoFrequencies.size()) ? gtoFrequencies[a] * 100.0 : 0.0;

        logStr += QString("%1: %2% (GTO: %3%)%4")
            .arg(actText)
            .arg(guessVal)
            .arg(QString::number(gtoVal, 'f', 1))
            .arg(a == numActions - 1 ? "" : ", ");
    }
    logStr += QString(" | Accuracy: %1%").arg(QString::number(accuracy, 'f', 1));
    logMessage(logStr);

    // Build beautiful HTML comparison table
    QString tableHtml = "<table border='1' cellpadding='5' cellspacing='0' bgcolor='#2c3e50' style='border-collapse: collapse; width: 100%; font-size: 13px; text-align: center; color: white; border-color: #34495e;'>";
    tableHtml += "<tr bgcolor='#34495e' style='font-weight: bold; color: white;'><th>Action</th><th>Your Guess</th><th>GTO Frequency</th><th>Difference</th></tr>";
    for (int a = 0; a < numActions; ++a) {
        QString actText = "";
        if (a < (int)actions.size()) {
            actText = QString::fromStdString(actions[a].toString());
        }
        actText.replace("BET_", "Bet ");
        actText.replace("RAISE_", "Raise ");
        actText.replace("CALL", "Call");
        actText.replace("CHECK", "Check");
        actText.replace("FOLD", "Fold");

        double guessVal = (a < (int)m_sliderValues.size()) ? (double)m_sliderValues[a] : 0.0;
        double gtoVal = (a < (int)gtoFrequencies.size()) ? gtoFrequencies[a] * 100.0 : 0.0;
        double diffVal = guessVal - gtoVal;

        QString diffStr = (diffVal >= 0.0 ? "+" : "") + QString::number(diffVal, 'f', 1) + "%";
        QString diffColor = (std::abs(diffVal) < 5.0) ? "#27ae60" : (std::abs(diffVal) < 15.0) ? "#e67e22" : "#c0392b";

        tableHtml += QString("<tr><td style='color: white;'><b>%1</b></td><td style='color: white;'>%2%</td><td style='color: white;'>%3%</td><td style='color: %4;'><b>%5</b></td></tr>")
            .arg(actText)
            .arg(QString::number(guessVal, 'f', 1))
            .arg(QString::number(gtoVal, 'f', 1))
            .arg(diffColor)
            .arg(diffStr);
    }
    tableHtml += "</table>";

    // Set feedback label HTML
    QString feedbackHtml = QString(
        "<div style='text-align: center; font-size: 14px; font-weight: bold; margin-bottom: 8px;'>Accuracy: <font color='%1'>%2%</font></div>"
        "%3"
    )
    .arg(accuracy >= 90.0 ? "#27ae60" : accuracy >= 70.0 ? "#e67e22" : "#c0392b")
    .arg(QString::number(accuracy, 'f', 1))
    .arg(tableHtml);

    ui->feedbackLabel->setStyleSheet("background-color: #2c3e50; color: white; border: 1px solid #34495e; border-radius: 6px; padding: 10px;");
    ui->feedbackLabel->setText(feedbackHtml);
    ui->feedbackLabel->show();

    // Sample next action from GTO distribution
    // Safety: ensure weights are valid for discrete_distribution
    double freqSum = 0.0;
    for (auto v : gtoFrequencies) {
        if (std::isnan(v) || std::isinf(v)) { freqSum = 0.0; break; }
        freqSum += v;
    }
    int sampledIdx = 0;
    if (freqSum > 0.0) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::discrete_distribution<int> dist(gtoFrequencies.begin(), gtoFrequencies.end());
        sampledIdx = dist(gen);
    }
    if (sampledIdx < 0 || sampledIdx >= (int)actionNode->getChildrens().size()) {
        sampledIdx = 0;
    }

    clearLayout(ui->actionButtonsLayout);
    if (sampledIdx >= 0 && sampledIdx < (int)actionNode->getChildrens().size()) {
        m_currentNode = actionNode->getChildrens()[sampledIdx];
    }

    ui->nextHandButton->setText("Continue ➡️");
    ui->nextHandButton->show();
}

void GtoTrainerWindow::updateStatsLabel()
{
    if (ui->rangeChallengeCheckBox->isChecked()) {
        QString accStr = "--";
        if (rangeChallengeTotal > 0) {
            accStr = QString::number((rangeChallengeAccuracySum / rangeChallengeTotal), 'f', 1) + "%";
        }
        ui->statsLabel->setText(QString("Hands: %1 | Range Acc: %2 | Quiz: %3/%4 | Narrow: %5/%6")
                                .arg(handsPlayed)
                                .arg(accStr)
                                .arg(m_quizCorrect)
                                .arg(m_quizTotal)
                                .arg(m_narrowingCorrect)
                                .arg(m_narrowingTotal));
    } else {
        QString accStr = "--";
        if (totalActions > 0) {
            accStr = QString::number((float)correctActions / totalActions * 100.0f, 'f', 1) + "%";
        }
        ui->statsLabel->setText(QString("Hands: %1 | Accuracy: %2 | EV Loss: %3 BB | Quiz: %4/%5 | Narrow: %6/%7")
                                .arg(handsPlayed)
                                .arg(accStr)
                                .arg(QString::number(totalEvLoss, 'f', 2))
                                .arg(m_quizCorrect)
                                .arg(m_quizTotal)
                                .arg(m_narrowingCorrect)
                                .arg(m_narrowingTotal));
    }
}
