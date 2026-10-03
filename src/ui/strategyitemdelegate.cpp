#include "include/ui/strategyitemdelegate.h"
#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QRect>
#include <QBrush>

StrategyItemDelegate::StrategyItemDelegate(QSolverJob * qSolverJob,DetailWindowSetting* detailWindowSetting,QObject *parent, int fontSize) :
    WordItemDelegate(parent), m_fontSize(fontSize)
{
    this->detailWindowSetting = detailWindowSetting;
    this->qSolverJob = qSolverJob;
}

void StrategyItemDelegate::setFontSize(int fontSize) {
    m_fontSize = fontSize;
}

void StrategyItemDelegate::paint_strategy(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, bool withEVs) const {
    auto options = option;
    initStyleOption(&options, index);
    const TableStrategyModel * tableStrategyModel = qobject_cast<const TableStrategyModel*>(index.model());
    if(tableStrategyModel->treeItem != NULL &&
            tableStrategyModel->treeItem->m_treedata.lock()->getType() == GameTreeNode::GameTreeNode::ACTION){
        shared_ptr<GameTreeNode> node = tableStrategyModel->treeItem->m_treedata.lock();
        vector<pair<GameActions,float>> strategy = tableStrategyModel->get_strategy(index.row(),index.column());
        vector<float> evs = tableStrategyModel->get_strategies_evs(index.row(),index.column());
        if(!strategy.empty()){
            float fold_prob = 0;
            vector<float> strategy_without_fold;
            vector<float> evs_without_fold;
            float strategy_without_fold_sum = 0;
            for(std::size_t i = 0;i < strategy.size();i ++){
                GameActions one_action = strategy[i].first;
                if(one_action.getAction() == GameTreeNode::PokerActions::FOLD){
                    fold_prob = strategy[i].second;
                }else{
                    strategy_without_fold.push_back(strategy[i].second);
                    strategy_without_fold_sum += strategy[i].second;
                    evs_without_fold.push_back(evs[i]);
                }
            }
            if(strategy_without_fold_sum > 0.){
                for(std::size_t i = 0;i < strategy_without_fold.size();i ++){
                    strategy_without_fold[i] = strategy_without_fold[i] / strategy_without_fold_sum;
                }
            }

            // get range data
            vector<pair<int,int>> card_cords;
            card_cords = tableStrategyModel->ui_strategy_table[index.row()][index.column()];
            float range_number = 0;
            if(!card_cords.empty()){
                for(auto one_cord:card_cords){
                    int playerToUse = (this->detailWindowSetting->targetPlayer != -1)
                                      ? this->detailWindowSetting->targetPlayer
                                      : tableStrategyModel->current_player;
                    if(0 == playerToUse){
                        range_number += tableStrategyModel->p1_range[one_cord.first][one_cord.second];
                    }else{
                        // when it's oop, which is p1
                        range_number += tableStrategyModel->p2_range[one_cord.first][one_cord.second];
                    }
                }
                range_number = range_number / card_cords.size();
                if(range_number < 0 || range_number > 1) throw runtime_error("range number incorrect in strategyitemdeletage");
            }
            float max_range_weight = 0.0f;
            int nRanks = (int)tableStrategyModel->ui_strategy_table.size();
            for (int r = 0; r < nRanks; ++r) {
                for (int c = 0; c < nRanks; ++c) {
                    const vector<pair<int,int>>& cell_cords = tableStrategyModel->ui_strategy_table[r][c];
                    if (cell_cords.empty()) continue;
                    float cell_weight = 0.0f;
                    for (auto& cd : cell_cords) {
                        int playerToUse = (this->detailWindowSetting->targetPlayer != -1)
                                          ? this->detailWindowSetting->targetPlayer
                                          : tableStrategyModel->current_player;
                        if(0 == playerToUse){
                            cell_weight += tableStrategyModel->p1_range[cd.first][cd.second];
                        }else{
                            cell_weight += tableStrategyModel->p2_range[cd.first][cd.second];
                        }
                    }
                    cell_weight /= cell_cords.size();
                    if (cell_weight > max_range_weight) max_range_weight = cell_weight;
                }
            }

            float normalized_range = (max_range_weight > 0.0001f) ? (range_number / max_range_weight) : 0.0f;
            if (normalized_range > 1.0f) normalized_range = 1.0f;
            if (normalized_range < 0.0f) normalized_range = 0.0f;

            int alpha = static_cast<int>(normalized_range * 255.0f);

            int disable_height = (int)(0.5+fold_prob * option.rect.height());
            int remain_height = option.rect.height() - disable_height;

            // draw background for flod
        if (disable_height > 0 && alpha > 0) {
            QRect rect(option.rect.left(), option.rect.top(),\
             option.rect.width(), disable_height);
            QBrush brush(QColor(0,191,255, alpha));
            painter->fillRect(rect, brush);
        }

        if (remain_height > 0 && alpha > 0){
            int ind = 0;
            float last_prob = 0;
            int bet_raise_num = 0;
            for(std::size_t i = 0;i < strategy.size();i ++){
                GameActions one_action = strategy[i].first;
                float normalized_ev = withEVs ? normalization_tanh(node->getPot() * 3, evs_without_fold[i]) : 1.;
                QBrush brush(Qt::gray);
                if(one_action.getAction() != GameTreeNode::PokerActions::FOLD){
                    if(one_action.getAction() == GameTreeNode::PokerActions::CHECK
                            || one_action.getAction() == GameTreeNode::PokerActions::CALL){
                        int green = 255;
                        int blue = max((int)(225 - normalized_ev * 175),55);
                        int red = 55;
                        brush = QBrush(QColor(red,green,blue, alpha));
                    }
                    else if(one_action.getAction() == GameTreeNode::PokerActions::BET
                            || one_action.getAction() == GameTreeNode::PokerActions::RAISE){
                        int color_base = max(128 - 32 * bet_raise_num - 1,0);
                        int blue = max((int)(255 - normalized_ev * (255 - color_base)), color_base);
                        int red = color_base + min((int)(normalized_ev * (255-color_base)),255-color_base);;
                        int green = color_base;
                        brush = QBrush(QColor(red,green,blue, alpha));

                        bet_raise_num += 1;
                    }else{
                        brush = QBrush(QColor(0, 0, 255, alpha));
                    }

                    int delta_x = (int)(option.rect.width() * last_prob);
                    int delta_width = (int)(option.rect.width() * (last_prob + strategy_without_fold[ind])) - (int)(option.rect.width() * last_prob);

                    QRect rect(option.rect.left() + delta_x, option.rect.top() + disable_height,\
                         delta_width , remain_height);
                    painter->fillRect(rect, brush);

                    last_prob += strategy_without_fold[ind];
                    ind += 1;
                }
            }
        }
        }
    }
    QTextDocument doc;
    if (m_fontSize > 0) {
        doc.setDefaultStyleSheet(QString("h4 { font-size: %1px; margin: 0px; padding: 0px; }").arg(m_fontSize));
    }
    doc.setHtml(options.text);

    options.text = "";
    //options.widget->style()->drawControl(QStyle::CE_ItemViewItem, &option, painter);

    painter->translate(options.rect.left(), options.rect.top());
    QRect clip(0, 0, options.rect.width(), options.rect.height());
    doc.drawContents(painter, clip);
}

void StrategyItemDelegate::paint_range(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    auto options = option;
    initStyleOption(&options, index);
    const TableStrategyModel * tableStrategyModel = qobject_cast<const TableStrategyModel*>(index.model());

    vector<pair<int,int>> card_cords;
    if(tableStrategyModel == NULL
            || tableStrategyModel->ui_p1_range.size() <= index.row()
            || tableStrategyModel->ui_p1_range.size() <= index.column()
            || tableStrategyModel->ui_p2_range.size() <= index.row()
            || tableStrategyModel->ui_p2_range.size() <= index.column()
            ){
        return;
    }
    if(this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP){
        card_cords = tableStrategyModel->ui_p1_range[index.row()][index.column()];
    }else{
        card_cords = tableStrategyModel->ui_p2_range[index.row()][index.column()];
    }

    if(tableStrategyModel->p1_range.empty() || tableStrategyModel->p2_range.empty()){
        return;
    }

    if(!card_cords.empty()){
        float range_number = 0;
        for(auto one_cord:card_cords){
            if(this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP){
                range_number += tableStrategyModel->p1_range[one_cord.first][one_cord.second];
            }else{
                // when it's oop, which is p1
                range_number += tableStrategyModel->p2_range[one_cord.first][one_cord.second];
            }
        }
        range_number = range_number / card_cords.size();

        // Find the max combo weight across the entire range for normalization.
        // This ensures postflop grids (where weights can be < 0.01) still render
        // visually meaningful fill heights rather than appearing nearly empty.
        float max_range_weight = 0.0f;
        const vector<vector<float>>& rangeRef = (this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP)
                ? tableStrategyModel->p1_range
                : tableStrategyModel->p2_range;

        // Scan ui_p1/p2_range to find max avg weight over any grid cell
        int nRanks = (int)tableStrategyModel->ui_p1_range.size();
        for (int r = 0; r < nRanks; ++r) {
            for (int c = 0; c < nRanks; ++c) {
                const vector<pair<int,int>>& cell_cords = (this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP)
                        ? tableStrategyModel->ui_p1_range[r][c]
                        : tableStrategyModel->ui_p2_range[r][c];
                if (cell_cords.empty()) continue;
                float cell_weight = 0.0f;
                for (auto& cd : cell_cords) {
                    cell_weight += rangeRef[cd.first][cd.second];
                }
                cell_weight /= cell_cords.size();
                if (cell_weight > max_range_weight) max_range_weight = cell_weight;
            }
        }

        // Normalize: express this cell's weight relative to the max weight
        float normalized_range = (max_range_weight > 0.0001f) ? (range_number / max_range_weight) : 0.0f;
        if (normalized_range > 1.0f) normalized_range = 1.0f;
        if (normalized_range < 0.0f) normalized_range = 0.0f;

        // Draw background with alpha-based yellow strength
        int alpha = static_cast<int>(normalized_range * 255.0f);
        if (alpha > 0) {
            QRect rect = option.rect;
            QBrush brush(QColor(254, 210, 3, alpha));
            painter->fillRect(rect, brush);
        }
    }
    QTextDocument doc;
    if (m_fontSize > 0) {
        doc.setDefaultStyleSheet(QString("h4 { font-size: %1px; margin: 0px; padding: 0px; }").arg(m_fontSize));
    }
    doc.setHtml(options.text);

    options.text = "";
    //options.widget->style()->drawControl(QStyle::CE_ItemViewItem, &option, painter);

    painter->translate(options.rect.left(), options.rect.top());
    QRect clip(0, 0, options.rect.width(), options.rect.height());
    doc.drawContents(painter, clip);
}

void StrategyItemDelegate::paint_evs(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    auto options = option;
    initStyleOption(&options, index);
    const TableStrategyModel * tableStrategyModel = qobject_cast<const TableStrategyModel*>(index.model());
    vector<float> evs = tableStrategyModel->get_ev_grid(index.row(),index.column());
    sort(evs.begin(), evs.end());

    int last_left = 0;
    for(std::size_t i = 0;i < evs.size();i ++ ){
        float one_ev = evs[evs.size() - i - 1];
        float normalized_ev = normalization_tanh(this->qSolverJob->stack,one_ev);
        //options.text += QString("</br>%1").arg(QString::number(normalized_ev));

        int red = max((int)(255 - normalized_ev * 255),0);
        int green = min((int)(normalized_ev * 255),255);
        int blue = min(red, green);

        QBrush brush = QBrush(QColor(red,green,blue));

        int this_width = (int)((float(i + 1) / evs.size()) * options.rect.width()) - last_left;

        QRect rect(option.rect.left() + last_left, option.rect.top(),\
             this_width , (int) (options.rect.height()));
        painter->fillRect(rect, brush);
        last_left += this_width;
    }
    QTextDocument doc;
    if (m_fontSize > 0) {
        doc.setDefaultStyleSheet(QString("h4 { font-size: %1px; margin: 0px; padding: 0px; }").arg(m_fontSize));
    }
    doc.setHtml(options.text);
    options.text = "";
    //options.widget->style()->drawControl(QStyle::CE_ItemViewItem, &option, painter);

    painter->translate(options.rect.left(), options.rect.top());
    QRect clip(0, 0, options.rect.width(), options.rect.height());
    doc.drawContents(painter, clip);
}

void StrategyItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {

    painter->save();

    QRect rect(option.rect.left(), option.rect.top(),\
             option.rect.width(), option.rect.height());
    QBrush brush(Qt::gray);
    if(index.column() == index.row())brush = QBrush(Qt::darkGray);
    painter->fillRect(rect, brush);

    if(this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::STRATEGY){
        this->paint_strategy(painter,option,index);
    }
    else if(this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_IP ||
            this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::RANGE_OOP ){
        this->paint_range(painter,option,index);
    }
    else if(this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::EV){
        this->paint_strategy(painter,option,index,true);
    }
    else if(this->detailWindowSetting->mode == DetailWindowSetting::DetailWindowMode::EV_ONLY){
        this->paint_evs(painter,option,index);
    }

    painter->restore();

    // Draw gold border for expression category highlight
    // Must be AFTER restore() because sub-paint methods translate the painter
    if (this->detailWindowSetting->hasExpressionHighlight &&
        index.row() >= 0 && index.row() < 13 &&
        index.column() >= 0 && index.column() < 13 &&
        this->detailWindowSetting->cellHighlight[index.row()][index.column()]) {
        painter->save();
        painter->setPen(QPen(QColor(255, 215, 0), 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        painter->restore();
    }

    if (this->detailWindowSetting->grid_i == index.row() &&
        this->detailWindowSetting->grid_j == index.column()) {
        painter->save();
        painter->setPen(QPen(Qt::black, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
        painter->restore();
    }
}
