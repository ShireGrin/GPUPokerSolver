#ifndef STRATEGYEXPLORER_H
#define STRATEGYEXPLORER_H

#include <QWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QFileDialog>
#include <QMessageBox>

#include "include/runtime/qsolverjob.h"
#include "QItemSelection"
#include "include/ui/worditemdelegate.h"
#include "include/ui/tablestrategymodel.h"
#include "include/ui/strategyitemdelegate.h"
#include "include/ui/detailwindowsetting.h"
#include "include/Card.h"
#include "include/ui/detailviewermodel.h"
#include "include/ui/detailitemdelegate.h"
#include "include/ui/roughstrategyviewermodel.h"
#include "include/ui/roughstrategyitemdelegate.h"
#include "include/nodes/GameTreeNode.h"
#include "include/nodes/ActionNode.h"
#include "include/nodes/ChanceNode.h"
#include "include/nodes/TerminalNode.h"
#include "include/nodes/ShowdownNode.h"

namespace Ui {
class StrategyExplorer;
}

class StrategyExplorer : public QWidget
{
    Q_OBJECT

public:
    explicit StrategyExplorer(QWidget *parent = 0,QSolverJob * qSolverJob=nullptr);
    ~StrategyExplorer();
    void setPreselectedCards(const QString& turn, const QString& river);
    TableStrategyModel* getTableStrategyModel() const { return tableStrategyModel; }

private:
    DetailWindowSetting detailWindowSetting;
    QTimer *timer;
    Ui::StrategyExplorer *ui;
    QSolverJob * qSolverJob;
    StrategyItemDelegate * delegate_strategy;
    TableStrategyModel * tableStrategyModel;
    class GtoTrainerWindow * gtoTrainerWindow = nullptr;
    DetailViewerModel * detailViewerModel;
    DetailItemDelegate * detailItemItemDelegate;
    RoughStrategyViewerModel * roughStrategyViewerModel;
    RoughStrategyItemDelegate * roughStrategyItemDelegate;
    vector<Card> cards;
    shared_ptr<GameTreeNode> selectedNode;  // Currently selected node in the tree
    class QLabel* solvingStatusLabel = nullptr;
    void process_treeclick(TreeItem* treeitem);
    void process_board(TreeItem* treeitem);
    void updateLockButtons();
    void updateSolvingStatus();

    // Interactive Node Lock Editor variables and methods
    int selectedGridRow = -1;
    int selectedGridCol = -1;
    int selectedComboRow = -1;
    int selectedComboCol = -1;
    bool isUpdatingSliders = false;
    bool painterModeActive = false;
    std::vector<class QSlider*> lockSliders;
    std::vector<class QDoubleSpinBox*> lockSliderLabels;
    void updateLockEditor();
    void rebuildSliders(int actionCount, const std::vector<std::string>& actionNames);
    void updateSliderValues(const std::vector<float>& strategy);
    void applyLockToSelection();  // Shared lock logic for both Lock button and painter
    void paintCell(int gridRow, int gridCol);
    void paintCombo(int comboRow, int comboCol);
    void erasePaintCell(int gridRow, int gridCol);
    void erasePaintCombo(int comboRow, int comboCol);
    bool isCellFullyLocked(int gridRow, int gridCol);
    bool isComboLocked(int comboRow, int comboCol);
    std::vector<float> getBrushStrategy();
    bool isCellLockedWithStrategy(int gridRow, int gridCol, const std::vector<float>& brushStrategy);
    bool isComboLockedWithStrategy(int comboRow, int comboCol, const std::vector<float>& brushStrategy);

    // Expression category selector methods
    bool comboMatchesCategory(int card1, int card2, const std::vector<int>& boardCardInts, const QString& category);
    void updateSlidersForCategory(const QString& category);
    std::vector<int> getCurrentBoardCardInts();
    void updateComboHighlightsForCell(int gridRow, int gridCol);

public slots:
    void item_expanded(const QModelIndex& index);
    void item_clicked(const QModelIndex& index);
    void selection_changed(const QItemSelection &selected,
                                            const QItemSelection &deselected);
private slots:
    void on_turnCardBox_currentIndexChanged(int index);
    void on_riverCardBox_currentIndexChanged(int index);
    void update_second();
    void onMouseMoveEvent(int i,int j);
    void on_strategyModeButtom_clicked();
    void on_ipRangeButtom_clicked();
    void on_oopRangeButtom_clicked();
    void on_evModeButtom_clicked();
    void on_evOnlyModeButtom_clicked();
    void on_dumpCsvButton_clicked();
    void on_lockCsvButton_clicked();
    void on_unlockButton_clicked();
    void on_gtoTrainerButton_clicked();

    // Interactive Node Lock Editor slots
    void onStrategyGridClicked(const QModelIndex &index);
    void onDetailViewClicked(const QModelIndex &index);
    void on_lockSelectionButton_clicked();
    void on_unlockSelectionButton_clicked();
    void onSliderValueChanged(int value);
    void onSpinBoxValueChanged(double value);
    void on_painterModeButton_toggled(bool checked);
    void on_expressionSelectBox_currentIndexChanged(int index);
};

#endif // STRATEGYEXPLORER_H
