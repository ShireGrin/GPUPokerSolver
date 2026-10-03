#ifndef PREFLOPSOLVERWINDOW_H
#define PREFLOPSOLVERWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QStandardItemModel>
#include <QTimer>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QItemSelection>
#include <memory>
#include <vector>
#include <map>
#include <string>

#include "include/runtime/qsolverjob.h"
#include "include/preflop/PreflopCfrSolver.h"
#include "include/preflop/PreflopEquityManager.h"
#include "include/preflop/PreflopGameTree.h"
#include "include/preflop/PreflopIcmCalculator.h"
#include "include/ranges/PrivateCards.h"
#include "include/compairer/Compairer.h"
#include "include/ui/pt4importdialog.h"

namespace Ui {
class PreflopSolverWindow;
}

// Forward declarations
class PreflopSolverThread;
class PreflopTableStrategyModel;
class PreflopStrategyItemDelegate;

class PreflopSolverWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit PreflopSolverWindow(QSolverJob* solverJob, QWidget *parent = nullptr);
    ~PreflopSolverWindow();

    void importPreflopHand(const ImportedHand& hand);

private slots:
    void onTableSizeChanged(int value);
    void onPlayerTableCellDoubleClicked(int row, int column);
    void onBuildTreeButtonClicked();
    void onSolveButtonClicked();
    void onStopButtonClicked();
    void onProgressTimerTimeout();
    void onSolvingFinished();
    void onTreeViewCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
    void onSolverProgress(int iteration, float exploitability, float avg_duration_ms, float first_duration_ms, float last_duration_ms);
    void onSaveButtonClicked();
    void onLoadButtonClicked();
    void onSolveFileClicked(const QModelIndex& index);
    void onGeneratePayoutsButtonClicked();
    void onTableViewSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onLockSelectionClicked();
    void onUnlockSelectionClicked();
    void onUnlockAllClicked();
    void onLockSliderValueChanged(int value);
    void onLockSpinBoxValueChanged(double value);

private:
    void log(const QString& msg);
    Ui::PreflopSolverWindow *ui;
    QSolverJob* qSolverJob;

    // Node Locking UI components
    QGroupBox* nodeLockGroupBox = nullptr;
    QLabel* lockSelectionLabel = nullptr;
    QHBoxLayout* lockSlidersLayout = nullptr;
    QPushButton* lockSelectionButton = nullptr;
    QPushButton* unlockSelectionButton = nullptr;
    QPushButton* unlockAllButton = nullptr;
    QPushButton* clearSelectionButton = nullptr;
    QList<QSlider*> lockSliders;
    QList<QDoubleSpinBox*> lockSpinBoxes;

    // Payout calculation UI components
    QComboBox* payoutTypeComboBox = nullptr;
    QComboBox* payoutPercentageComboBox = nullptr;
    QSpinBox* entrantsSpinBox = nullptr;

    // Solver engine objects
    shared_ptr<PreflopGameTree> gameTree;
    shared_ptr<PreflopGameTreeNode> treeRoot;
    shared_ptr<PreflopEquityManager> equityManager;
    shared_ptr<PreflopCfrSolver> cfrSolver;
    PreflopIcmCalculator icmCalc;

    // Ranges for players
    vector<vector<PrivateCards>> current_player_ranges;
    int current_num_players;

    // View Models & Delegates
    QStandardItemModel* treeModel;
    PreflopTableStrategyModel* strategyModel;
    PreflopStrategyItemDelegate* strategyDelegate;

    // Background Thread & Timer
    PreflopSolverThread* solverThread;
    QTimer* progressTimer;

    // Mapping raw node pointer to shared_ptr for tree selection
    std::map<void*, shared_ptr<PreflopGameTreeNode>> nodeMap;

    QFileSystemModel* solvesModel;
    QSortFilterProxyModel* solvesProxyModel;

    QList<QPushButton*> rangeViewButtons;
    QPushButton* gtoStrategyButton = nullptr;
    QHBoxLayout* rangeViewButtonsLayout = nullptr;
    QList<QStandardItem*> highlightedTreeItems;

    void updateRangeViewButtons();
    void updateStartingRangeButtonsUI();
    void setIndexExpandedRecursive(const QModelIndex& index, bool expanded);

    void updatePlayersTable();
    void populateTreeModel(QStandardItem* parentItem, const shared_ptr<PreflopGameTreeNode>& node, int num_players);
    vector<string> get_positions(int num_players);
    void initialize_trainables(const shared_ptr<PreflopGameTreeNode>& node, const vector<vector<PrivateCards>>& player_ranges);
    void loadSolveFromFile(const QString& fileName);

    vector<int> getSelectedHandIndices(int player, const QModelIndexList& selected);
    void updateLockEditorUI();
    void rebuildLockSliders(int action_count, const vector<string>& actions);
};

// Background solver thread
class PreflopSolverThread : public QThread {
    Q_OBJECT
private:
    shared_ptr<PreflopCfrSolver> solver;

public:
    PreflopSolverThread(shared_ptr<PreflopCfrSolver> solver, QObject* parent = nullptr)
        : QThread(parent), solver(solver) {}

    void run() override {
        if (solver) {
            solver->set_progress_callback([this](int iter, float expl, float avg_t, float first_t, float last_t) {
                emit solverProgress(iter, expl, avg_t, first_t, last_t);
            });
            solver->train();
        }
    }

signals:
    void solverProgress(int iteration, float exploitability, float avg_duration_ms, float first_duration_ms, float last_duration_ms);
};

// 13x13 Strategy Grid Table Model
class PreflopTableStrategyModel : public QAbstractTableModel {
    Q_OBJECT
private:
    QStringList ranks;
    shared_ptr<PreflopGameTreeNode> active_node;
    std::vector<std::vector<float>> all_reach_probs;
    std::vector<std::vector<PrivateCards>> player_ranges;
    int viewing_starting_range_player = -1;
    vector<float> reach_probs;

public:
    enum CustomDataRoles {
        StrategyDataRole = Qt::UserRole,
        IsLockedRole,
        EvDataRole
    };

    PreflopTableStrategyModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {
        ranks << "A" << "K" << "Q" << "J" << "T" << "9" << "8" << "7" << "6" << "5" << "4" << "3" << "2";
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return 13; }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override { return 13; }

    void setNodeAndRanges(shared_ptr<PreflopGameTreeNode> node, const vector<vector<PrivateCards>>& ranges) {
        beginResetModel();
        active_node = node;
        player_ranges = ranges;
        
        all_reach_probs.clear();
        if (active_node) {
            int num_players = ranges.size();
            all_reach_probs.resize(num_players);
            
            auto current = active_node;
            vector<shared_ptr<PreflopGameTreeNode>> path;
            while (current) {
                path.push_back(current);
                current = current->getParent();
            }
            std::reverse(path.begin(), path.end());
            
            for (int player = 0; player < num_players; ++player) {
                int hand_num = ranges[player].size();
                all_reach_probs[player].assign(hand_num, 1.0f);
                
                for (size_t i = 0; i < path.size() - 1; ++i) {
                    auto anc = path[i];
                    auto nxt = path[i+1];
                    if (anc->getType() == PreflopGameTreeNode::NodeType::ACTION) {
                        auto anc_action = dynamic_pointer_cast<PreflopActionNode>(anc);
                        if (anc_action->getPlayer() == player) {
                            const auto& acts = anc_action->getActions();
                            int a_idx = -1;
                            for (size_t a = 0; a < acts.size(); ++a) {
                                if (acts[a] == nxt->getPathAction()) {
                                    a_idx = a; break;
                                }
                            }
                            if (a_idx >= 0) {
                                auto tr = anc_action->getTrainable();
                                if (tr) {
                                    const auto& avg_strat = tr->getAverageStrategy();
                                    for (int h = 0; h < hand_num; ++h) {
                                        all_reach_probs[player][h] *= avg_strat[a_idx * hand_num + h];
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Set the old reach_probs to the active player for backwards compatibility
            if (active_node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
                auto action_node = dynamic_pointer_cast<PreflopActionNode>(active_node);
                int active_player = action_node->getPlayer();
                if (active_player >= 0 && active_player < num_players) {
                    reach_probs = all_reach_probs[active_player];
                }
            }
        }
        
        endResetModel();
    }

    void setViewingStartingRangePlayer(int player_idx) {
        beginResetModel();
        viewing_starting_range_player = player_idx;
        endResetModel();
    }

    void refresh() {
        emit dataChanged(index(0, 0), index(12, 12));
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
};

// Item delegate to paint action colors
class PreflopStrategyItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit PreflopStrategyItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QColor getActionColor(const string& action) const;
};

#endif // PREFLOPSOLVERWINDOW_H
