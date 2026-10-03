#ifndef GTOTRAINERWINDOW_H
#define GTOTRAINERWINDOW_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QSlider>
#include <map>
#include <vector>
#include <memory>
#include <random>
#include <functional>

#include "include/runtime/qsolverjob.h"
#include "include/nodes/GameTreeNode.h"
#include "include/nodes/ActionNode.h"
#include "include/nodes/ChanceNode.h"
#include "include/nodes/TerminalNode.h"
#include "include/nodes/ShowdownNode.h"
#include "include/ui/tablestrategymodel.h"
#include "include/ui/strategyitemdelegate.h"
#include "include/ui/detailwindowsetting.h"
#include "include/ui/treeitem.h"
#include "include/ui/handhistoryparser.h"
#include "include/Card.h"
#include "include/compairer/Dic5Compairer.h"

namespace Ui {
class GtoTrainerWindow;
}

class GtoTrainerWindow : public QDialog
{
    Q_OBJECT

public:
    enum class OpponentProfile {
        GTO,
        CALLING_STATION,
        NIT,
        MANIAC
    };

    explicit GtoTrainerWindow(QWidget *parent = nullptr, QSolverJob *qSolverJob = nullptr, shared_ptr<GameTreeNode> startNode = nullptr);
    ~GtoTrainerWindow();

private slots:
    void onStartSessionButtonClicked();
    void onNextHandButtonClicked();
    void onActionButtonClicked(int actionIndex);
    void onProjectionButtonClicked(int playerIndex, int actionIndex);
    void onNormalRangeButtonClicked(int playerIndex);

private:
    void startNewHand();
    void processGameState();
    void displayActionButtons();
    void displayFeedback(int chosenActionIdx, float chosenEv, float maxEv, float evLoss);
    void updateRangeViews();
    void logMessage(const QString& msg);
    HandGameState buildCurrentGameState();
    std::vector<Card> getBoardAtNode(shared_ptr<GameTreeNode> node);
    std::vector<Card> getChanceCardsAtNode(shared_ptr<GameTreeNode> node);
    std::pair<double, double> getPlayerStacks(shared_ptr<GameTreeNode> node);
    std::vector<float> adjustStrategyForProfile(const std::vector<float>& strategy, const std::vector<GameActions>& actions, OpponentProfile profile);
    void clearLayout(QLayout* layout);

    std::vector<std::vector<float>> computeProjectedRange(int playerIdx, int actionIdx, shared_ptr<ActionNode> node, const std::vector<std::vector<float>>& baseRange);
    std::vector<std::vector<float>> generateStrategicMistakeRange(const std::vector<std::vector<float>>& correctRange, const std::vector<std::vector<float>>& baseRange, std::mt19937& gen);
    float computeRangeWeightSum(int playerIdx);
    void showRangeMatcherQuiz(int opponentPlayer, int chosenActionIdx, shared_ptr<ActionNode> node, std::function<void()> onComplete);
    void showNarrowingChallenge(int opponentPlayer, std::function<void()> onComplete);
    void displayRangeChallengeSliders();
    void onSliderValueChanged(int sliderIdx, int value);
    void onSubmitRangeStrategy();
    void updateStatsLabel();

    Ui::GtoTrainerWindow *ui;
    QSolverJob *qSolverJob;
    shared_ptr<GameTreeNode> m_startNode;
    shared_ptr<GameTreeNode> m_currentNode;

    TreeItem* m_startNodeItem;
    TreeItem* m_currentNodeItem;

    int userPlayerSelection; // 0 = IP, 1 = OOP, 2 = Random
    int activeUserRole;      // 0 = IP (Player 1), 1 = OOP (Player 2)
    OpponentProfile oppProfile;

    Card userCard1, userCard2;
    Card oppCard1, oppCard2;
    std::vector<Card> activeBoard;
    Card turnCard;
    Card riverCard;

    // Statistics
    int handsPlayed;
    int correctActions;
    int totalActions;
    float totalEvLoss;
    bool sessionActive;

    // Quiz and Narrowing stats
    bool m_streetTransitionOccurred;
    float m_streetStartRangeWeightSum;
    int m_quizCorrect;
    int m_quizTotal;
    int m_narrowingCorrect;
    int m_narrowingTotal;

    std::map<int, Card> cardint2card;

    TableStrategyModel *p1TableModel;
    TableStrategyModel *p2TableModel;
    StrategyItemDelegate *p1Delegate;
    StrategyItemDelegate *p2Delegate;
    DetailWindowSetting p1Setting;
    DetailWindowSetting p2Setting;

    // Store the true range at the current node to restore from projections
    std::vector<std::vector<float>> trueP1Range;
    std::vector<std::vector<float>> trueP2Range;

    // Button groups for projections
    QButtonGroup* p1ButtonGroup;
    QButtonGroup* p2ButtonGroup;

    // Range Challenge stats and UI state
    int rangeChallengeTotal;
    double rangeChallengeAccuracySum;
    std::vector<QSlider*> m_rangeSliders;
    std::vector<QLabel*> m_rangeSliderLabels;
    std::vector<int> m_sliderValues;
    bool m_updatingSliders;
    QLabel* m_sliderSumLabel = nullptr;
    int m_dbHandId = -1;
};

#endif // GTOTRAINERWINDOW_H
