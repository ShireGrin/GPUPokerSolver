#include "include/ui/detailviewermodel.h"

DetailViewerModel::DetailViewerModel(TableStrategyModel* tableStrategyModel, DetailWindowSetting* detailWindowSetting, QObject *parent)
    : QAbstractItemModel(parent)
{
    this->tableStrategyModel = tableStrategyModel;
    this->detailWindowSetting = detailWindowSetting;
    this->columns = 4;
    this->rows = 3;
}

DetailViewerModel::~DetailViewerModel()
{
}

QModelIndex DetailViewerModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    return createIndex(row, column, nullptr);
}

QVariant DetailViewerModel::headerData(int section, Qt::Orientation orientation, int role){
    return QString::fromStdString("");
}

QModelIndex DetailViewerModel::parent(const QModelIndex &child) const{
    return QModelIndex();
}

int DetailViewerModel::columnCount(const QModelIndex &parent) const
{
    return this->columns;
}

int DetailViewerModel::rowCount(const QModelIndex &parent) const
{
    return this->rows;
}

QVariant DetailViewerModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    int col = index.column();

    if (role == Qt::ToolTipRole) {
        if (!tableStrategyModel || !detailWindowSetting) return QVariant();
        if (detailWindowSetting->grid_i < 0 || detailWindowSetting->grid_j < 0) return QVariant();

        // Get the list of card coordinates based on mode
        std::vector<std::pair<int,int>> card_cords;
        if (detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP) {
            if (detailWindowSetting->grid_i < (int)tableStrategyModel->ui_p1_range.size() &&
                detailWindowSetting->grid_j < (int)tableStrategyModel->ui_p1_range[detailWindowSetting->grid_i].size()) {
                card_cords = tableStrategyModel->ui_p1_range[detailWindowSetting->grid_i][detailWindowSetting->grid_j];
            }
        } else if (detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_OOP) {
            if (detailWindowSetting->grid_i < (int)tableStrategyModel->ui_p2_range.size() &&
                detailWindowSetting->grid_j < (int)tableStrategyModel->ui_p2_range[detailWindowSetting->grid_i].size()) {
                card_cords = tableStrategyModel->ui_p2_range[detailWindowSetting->grid_i][detailWindowSetting->grid_j];
            }
        } else {
            if (detailWindowSetting->grid_i < (int)tableStrategyModel->ui_strategy_table.size() &&
                detailWindowSetting->grid_j < (int)tableStrategyModel->ui_strategy_table[detailWindowSetting->grid_i].size()) {
                card_cords = tableStrategyModel->ui_strategy_table[detailWindowSetting->grid_i][detailWindowSetting->grid_j];
            }
        }

        int ind = row * columns + col;
        if (ind < 0 || ind >= (int)card_cords.size()) return QVariant();

        std::pair<int,int> cord = card_cords[ind];
        int card1 = cord.first;
        int card2 = cord.second;

        // Construct the rich HTML tooltip for this specific combo (card1, card2)
        QString comboHtml = tableStrategyModel->cardint2card[card1].toFormattedHtml() +
                            tableStrategyModel->cardint2card[card2].toFormattedHtml();

        QString tooltip = QString("<h3>%1</h3>").arg(comboHtml);

        // 1. Weight (Range Probability) for this combo
        float range_number = 0.0f;
        bool isP1 = true;
        if (detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP) {
            isP1 = true;
        } else if (detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_OOP) {
            isP1 = false;
        } else {
            isP1 = (0 == tableStrategyModel->current_player);
        }

        const auto& range_matrix = isP1 ? tableStrategyModel->p1_range : tableStrategyModel->p2_range;
        if (card1 < (int)range_matrix.size() && card2 < (int)range_matrix[card1].size()) {
            range_number = range_matrix[card1][card2];
        }

        tooltip += QString("<p><b>Weight (Range):</b> %1%</p>")
                .arg(QString::number(range_number * 100.0, 'f', 1));

        // 2. Frequencies and EVs for all actions for this specific combo
        if (tableStrategyModel->treeItem) {
            std::shared_ptr<GameTreeNode> node = tableStrategyModel->treeItem->m_treedata.lock();
            if (node && node->getType() == GameTreeNode::GameTreeNode::ACTION) {
                std::shared_ptr<ActionNode> actionNode = std::dynamic_pointer_cast<ActionNode>(node);
                std::vector<GameActions>& gameActions = actionNode->getActions();

                // Frequencies for this specific combo
                std::vector<float> strategy;
                if (card1 < (int)tableStrategyModel->current_strategy.size() &&
                    card2 < (int)tableStrategyModel->current_strategy[card1].size()) {
                    strategy = tableStrategyModel->current_strategy[card1][card2];
                }

                // EVs for this specific combo
                std::vector<float> evs;
                if (card1 < (int)tableStrategyModel->current_evs.size() &&
                    card2 < (int)tableStrategyModel->current_evs[card1].size()) {
                    evs = tableStrategyModel->current_evs[card1][card2];
                }

                if (!strategy.empty() && strategy.size() == gameActions.size()) {
                    tooltip += "<p><b>GTO Frequencies &amp; EVs:</b><br>";
                    for (size_t k = 0; k < strategy.size(); ++k) {
                        QString actName = tableStrategyModel->formatActionName(gameActions[k], node);
                        QString freqStr = QString::number(strategy[k] * 100.0, 'f', 1) + "%";
                        QString evStr = "N/A";
                        if (k < evs.size() && evs[k] == evs[k]) { // check for NaN
                            evStr = QString::number(evs[k], 'f', 2);
                        }
                        tooltip += QString("- <b>%1:</b> %2 (EV: %3)<br>")
                                .arg(actName)
                                .arg(freqStr)
                                .arg(evStr);
                    }
                    tooltip += "</p>";
                }
            }
        }

        // 3. Average EV for this combo
        float combo_avg_ev = 0.0f;
        bool has_combo_ev = false;
        if (card1 < (int)tableStrategyModel->current_strategy.size() &&
            card2 < (int)tableStrategyModel->current_strategy[card1].size() &&
            card1 < (int)tableStrategyModel->current_evs.size() &&
            card2 < (int)tableStrategyModel->current_evs[card1].size()) {
            const std::vector<float>& one_strategy = tableStrategyModel->current_strategy[card1][card2];
            const std::vector<float>& one_ev = tableStrategyModel->current_evs[card1][card2];
            if (one_strategy.size() == one_ev.size() && !one_strategy.empty()) {
                has_combo_ev = true;
                for (size_t k = 0; k < one_strategy.size(); ++k) {
                    if (one_ev[k] == one_ev[k]) { // check for NaN
                        combo_avg_ev += one_strategy[k] * one_ev[k];
                    }
                }
            }
        }
        if (has_combo_ev) {
            tooltip += QString("<p><b>Avg EV:</b> %1</p>")
                    .arg(QString::number(combo_avg_ev, 'f', 2));
        }

        return tooltip;
    }

    return "Viewer";
}

void DetailViewerModel::clicked_event(const QModelIndex & index){
}
