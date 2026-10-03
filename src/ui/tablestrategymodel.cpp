#include "include/ui/tablestrategymodel.h"

TableStrategyModel::TableStrategyModel(QSolverJob * data, QObject *parent)
    : QAbstractItemModel(parent)
{
    this->qSolverJob = data;
    setupModelData();
}

TableStrategyModel::~TableStrategyModel()
{
}

QModelIndex TableStrategyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    return createIndex(row, column, nullptr);
}

QVariant TableStrategyModel::headerData(int section, Qt::Orientation orientation, int role){
    return QString::fromStdString("");
}

QModelIndex TableStrategyModel::parent(const QModelIndex &child) const{
    return QModelIndex();
}

int TableStrategyModel::columnCount(const QModelIndex &parent) const
{
    return this->qSolverJob->get_solver()->get_deck()->getRanks().size();
}

int TableStrategyModel::rowCount(const QModelIndex &parent) const
{
    return this->qSolverJob->get_solver()->get_deck()->getRanks().size();
}

QVariant TableStrategyModel::data(const QModelIndex &index, int role) const
{
    vector<string>ranks = this->qSolverJob->get_solver()->get_deck()->getRanks();
    int row = index.row();
    int col = index.column();
    int larger = row > col?row:col;
    int smaller = row > col?col:row;

    QString suffix = "";
    if(row > col){
        suffix = tr("o");
    }else if(row < col){
        suffix = tr("s");
    }else{
        suffix = tr(" ");
    }

    QString handName = QString("%1%2%3")
            .arg(QString::fromStdString(ranks[ranks.size() - 1 - smaller]))
            .arg(QString::fromStdString(ranks[ranks.size() - 1 - larger]))
            .arg(suffix);

    if (role == Qt::ToolTipRole) {
        QString tooltip = QString("<h3>%1</h3>").arg(handName);
        
        // Calculate weight (range probability)
        float range_number = 0;
        if (row >= 0 && row < (int)ui_strategy_table.size() && col >= 0 && col < (int)ui_strategy_table[row].size()) {
            vector<pair<int,int>> card_cords = ui_strategy_table[row][col];
            const vector<vector<float>> *current_range = (0 == current_player ) ? & p1_range : & p2_range;
            if(!current_range->empty() && !card_cords.empty()){
                for(std::size_t indi = 0; indi < card_cords.size(); indi ++){
                    if (card_cords[indi].first < (int)current_range->size() && card_cords[indi].second < (int)(*current_range)[card_cords[indi].first].size()) {
                        range_number += (*current_range)[card_cords[indi].first][card_cords[indi].second];
                    }
                }
                range_number = range_number / card_cords.size();
            }
        }
        tooltip += QString("<p><b>Weight (Range):</b> %1%</p>")
                .arg(QString::number(range_number * 100.0, 'f', 1));

        // Get GTO strategies
        vector<pair<GameActions,float>> strategy = get_strategy(row, col);
        vector<float> evs = get_strategies_evs(row, col);
        
        if (!strategy.empty() && this->treeItem) {
            tooltip += "<p><b>GTO Frequencies &amp; EVs:</b><br>";
            shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();
            for (size_t k = 0; k < strategy.size(); ++k) {
                QString actName = formatActionName(strategy[k].first, node);
                QString freqStr = QString::number(strategy[k].second * 100.0, 'f', 1) + "%";
                QString evStr = "N/A";
                if (k < evs.size() && evs[k] != 0.0) {
                    evStr = QString::number(evs[k], 'f', 2);
                }
                tooltip += QString("- <b>%1:</b> %2 (EV: %3)<br>")
                        .arg(actName)
                        .arg(freqStr)
                        .arg(evStr);
            }
            tooltip += "</p>";
        }
        
        // Average EV
        vector<float> individual_evs = get_ev_grid(row, col);
        if (!individual_evs.empty()) {
            float total_ev = 0;
            for (float ev : individual_evs) {
                total_ev += ev;
            }
            float avg_ev = total_ev / individual_evs.size();
            tooltip += QString("<p><b>Avg EV:</b> %1</p>")
                    .arg(QString::number(avg_ev, 'f', 2));
        }

        return tooltip;
    }

    QString retval = QString("<h4>%1%2<b>%3</b></h4>")
            .arg(QString::fromStdString(ranks[ranks.size() - 1 - smaller]))
            .arg(QString::fromStdString(ranks[ranks.size() - 1 - larger]))
            .arg(suffix);
    return retval;
}

void TableStrategyModel::setupModelData()
{
    vector<Card> cards = this->qSolverJob->get_solver()->get_deck()->getCards();
    vector<string> ranks = this->qSolverJob->get_solver()->get_deck()->getRanks();
    for(auto one_card: cards){
        this->cardint2card.insert(std::pair<int, Card>(one_card.getCardInt(), one_card));
    }

    this->ui_strategy_table = vector<vector<vector<pair<int,int>>>>(ranks.size());
    for(std::size_t i = 0;i < ranks.size();i ++){
        this->ui_strategy_table[i] = vector<vector<pair<int,int>>>(ranks.size());
        for(std::size_t j = 0;j < ranks.size();j ++){
            this->ui_strategy_table[i][j] = vector<pair<int,int>>();
        }
    }

    this->p1_range = vector<vector<float>>(52);
    for(std::size_t i = 0;i < 52;i ++){
        this->p1_range[i] = vector<float>(52);
        for(std::size_t j = 0;j < 52;j ++){
            this->p1_range[i][j] = 0;
        }
    }

    this->p2_range = vector<vector<float>>(52);
    for(std::size_t i = 0;i < 52;i ++){
        this->p2_range[i] = vector<float>(52);
        for(std::size_t j = 0;j < 52;j ++){
            this->p2_range[i][j] = 0;
        }
    }

    this->ui_p1_range = vector<vector<vector<pair<int,int>>>>(ranks.size());
    for(std::size_t i = 0;i < ranks.size();i ++){
        this->ui_p1_range[i] = vector<vector<pair<int,int>>>(ranks.size());
        for(std::size_t j = 0;j < ranks.size();j ++){
            this->ui_p1_range[i][j] = vector<pair<int,int>>();
        }
    }

    this->ui_p2_range = vector<vector<vector<pair<int,int>>>>(ranks.size());
    for(std::size_t i = 0;i < ranks.size();i ++){
        this->ui_p2_range[i] = vector<vector<pair<int,int>>>(ranks.size());
        for(std::size_t j = 0;j < ranks.size();j ++){
            this->ui_p2_range[i][j] = vector<pair<int,int>>();
        }
    }

    vector<PrivateCards>& p1range = this->qSolverJob->get_solver()->player1Range;
    vector<PrivateCards>& p2range = this->qSolverJob->get_solver()->player2Range;

    for(PrivateCards one_private: p1range){
        Card card1 = this->cardint2card[one_private.card1];
        Card card2 = this->cardint2card[one_private.card2];

        int rank1 = card1.getCardInt() / 4;
        int suit1 = card1.getCardInt() - (rank1)*4;
        int index1 = 12 - rank1; // this index is the index of the actal ui, so AKQ would be the lower index and 234 would be high

        int rank2 = card2.getCardInt() / 4;
        int suit2 = card2.getCardInt() - (rank2)*4;
        int index2 = 12 - rank2;

        if(index1 == index2){
            this->ui_p1_range[index1][index2].push_back(std::pair<int,int>(one_private.card1,one_private.card2));
        }
        else if(suit1 == suit2){
            this->ui_p1_range[min(index1,index2)][max(index1,index2)].push_back(std::pair<int,int>(one_private.card1,one_private.card2));
        }else{
            this->ui_p1_range[max(index1,index2)][min(index1,index2)].push_back(std::pair<int,int>(one_private.card1,one_private.card2));
        }
    }
    for(PrivateCards one_private: p2range){
        Card card1 = this->cardint2card[one_private.card1];
        Card card2 = this->cardint2card[one_private.card2];

        int rank1 = card1.getCardInt() / 4;
        int suit1 = card1.getCardInt() - (rank1)*4;
        int index1 = 12 - rank1; // this index is the index of the actal ui, so AKQ would be the lower index and 234 would be high

        int rank2 = card2.getCardInt() / 4;
        int suit2 = card2.getCardInt() - (rank2)*4;
        int index2 = 12 - rank2;

        if(index1 == index2){
            this->ui_p2_range[index1][index2].push_back(std::pair<int,int>(one_private.card1,one_private.card2));
        }
        else if(suit1 == suit2){
            this->ui_p2_range[min(index1,index2)][max(index1,index2)].push_back(std::pair<int,int>(one_private.card1,one_private.card2));
        }else{
            this->ui_p2_range[max(index1,index2)][min(index1,index2)].push_back(std::pair<int,int>(one_private.card1,one_private.card2));
        }
    }
    this->total_strategy = vector<pair<GameActions,pair<float,float>>>();
}

void TableStrategyModel::clicked_event(const QModelIndex & index){
}

void TableStrategyModel::setGameTreeNode(TreeItem* treeNode){
    this->treeItem = treeNode;
}

void TableStrategyModel::setTrunCard(Card turn_card){
    this->turn_card = turn_card;
}

void TableStrategyModel::setRiverCard(Card river_card){
    this->river_card = river_card;
}

void TableStrategyModel::updateStrategyData(){
    if(this->treeItem != NULL){
        shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();
        this->setupModelData();
        if(node != nullptr && node->getType() == GameTreeNode::GameTreeNode::ACTION){
            shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(node);
            //actionNode->getTrainable();
            // create a vector card and input it to solver and get the result
            vector<Card> deal_cards;
            GameTreeNode::GameRound root_round = this->qSolverJob->get_solver()->getGameTree()->getRoot()->getRound();
            GameTreeNode::GameRound current_round = actionNode->getRound();
            this->current_player = actionNode->getPlayer();
            if(root_round == GameTreeNode::GameRound::FLOP){
                if(current_round == GameTreeNode::GameRound::TURN){deal_cards.push_back(this->turn_card);}
                if(current_round == GameTreeNode::GameRound::RIVER){deal_cards.push_back(this->turn_card);deal_cards.push_back(this->river_card);}
            }
            else if(root_round == GameTreeNode::GameRound::TURN){
                if(current_round == GameTreeNode::GameRound::RIVER){deal_cards.push_back(this->river_card);}
            }
            if(this->qSolverJob->get_solver() != NULL && this->qSolverJob->get_solver()->get_solver() != NULL){
                vector<vector<vector<float>>> current_strategy = this->qSolverJob->get_solver()->get_solver()->get_strategy(actionNode,deal_cards);
                this->current_strategy = current_strategy;

                vector<vector<vector<float>>> current_evs = this->qSolverJob->get_solver()->get_solver()->get_evs(actionNode,deal_cards);
                this->current_evs = current_evs;

                for(int i = 0;i < 52;i ++){
                    for(int j = 0;j < 52;j ++){
                        const vector<float>& one_strategy = this->current_strategy[i][j];
                        if(one_strategy.empty())continue;
                        Card card1 = this->cardint2card[i];
                        Card card2 = this->cardint2card[j];

                        int rank1 = card1.getCardInt() / 4;
                        int suit1 = card1.getCardInt() - (rank1)*4;
                        int index1 = 12 - rank1; // this index is the index of the actal ui, so AKQ would be the lower index and 234 would be high

                        int rank2 = card2.getCardInt() / 4;
                        int suit2 = card2.getCardInt() - (rank2)*4;
                        int index2 = 12 - rank2;

                        if(index1 == index2){
                            this->ui_strategy_table[index1][index2].push_back(std::pair<int,int>(i,j));
                        }
                        else if(suit1 == suit2){
                            this->ui_strategy_table[min(index1,index2)][max(index1,index2)].push_back(std::pair<int,int>(i,j));
                        }else{
                            this->ui_strategy_table[max(index1,index2)][min(index1,index2)].push_back(std::pair<int,int>(i,j));
                        }
                    }
                }
            }
        }
        if(this->qSolverJob->get_solver() != NULL  && this->qSolverJob->get_solver()->get_solver() != NULL && node != nullptr){
            vector<PrivateCards>& p1range = this->qSolverJob->get_solver()->player1Range;
            vector<PrivateCards>& p2range = this->qSolverJob->get_solver()->player2Range;

            for(int i = 0; i < 52; i++) {
                for(int j = 0; j < 52; j++) {
                    this->p1_private_id[i][j] = -1;
                    this->p2_private_id[i][j] = -1;
                }
            }

            for(size_t i = 0; i < p1range.size(); i++) {
                auto one_private = p1range[i];
                this->p1_range[one_private.card1][one_private.card2] = one_private.weight;
                this->p1_private_id[one_private.card1][one_private.card2] = i;
            }
            for(size_t i = 0; i < p2range.size(); i++) {
                auto one_private = p2range[i];
                this->p2_range[one_private.card1][one_private.card2] = one_private.weight;
                this->p2_private_id[one_private.card1][one_private.card2] = i;
            }

            shared_ptr<GameTreeNode> iter_node = node->getParent();
            shared_ptr<GameTreeNode> last_node = node;
            while(iter_node != nullptr){
                if(iter_node->getType() == GameTreeNode::GameTreeNode::ACTION){
                    shared_ptr<ActionNode> iterActionNode = dynamic_pointer_cast<ActionNode>(iter_node);
                    vector<Card> deal_cards;
                    GameTreeNode::GameRound root_round = this->qSolverJob->get_solver()->getGameTree()->getRoot()->getRound();
                    GameTreeNode::GameRound current_round = iterActionNode->getRound();
                    if(root_round == GameTreeNode::GameRound::FLOP){
                        if(current_round == GameTreeNode::GameRound::TURN){deal_cards.push_back(this->turn_card);}
                        if(current_round == GameTreeNode::GameRound::RIVER){deal_cards.push_back(this->turn_card);deal_cards.push_back(this->river_card);}
                    }
                    else if(root_round == GameTreeNode::GameRound::TURN){
                        if(current_round == GameTreeNode::GameRound::RIVER){deal_cards.push_back(this->river_card);}
                    }

                    vector<vector<vector<float>>> current_strategy = this->qSolverJob->get_solver()->get_solver()->get_strategy(iterActionNode,deal_cards);

                    int child_chosen = -1;
                    for(std::size_t i = 0;i < iterActionNode->getChildrens().size();i ++){
                        if(iterActionNode->getChildrens()[i] == last_node){
                            child_chosen = i;
                            break;
                        }
                    }
                    if(child_chosen == -1)throw runtime_error("no child chosen");
                    for(std::size_t i = 0;i < 52;i ++){
                        for(std::size_t j = 0;j < 52;j ++){
                            if(current_strategy[i][j].size() == 0)continue;
                            if(iterActionNode->getPlayer() == 0){ // p1, IP
                                this->p1_range[i][j] *= current_strategy[i][j][child_chosen];
                            }
                            else if(iterActionNode->getPlayer() == 1){ // p2, OOP
                                this->p2_range[i][j] *= current_strategy[i][j][child_chosen];
                            }else throw runtime_error("player not exist in tablestrategymodel");
                        }
                    }
                }

                iter_node = iter_node->getParent();
                last_node = last_node->getParent();
            }
        }
        if(this->qSolverJob->get_solver() != NULL  && this->qSolverJob->get_solver()->get_solver() != NULL && node != nullptr){
            this->total_strategy = this->get_total_strategy();
        }
    }
}

const vector<pair<GameActions,pair<float,float>>> TableStrategyModel::get_total_strategy() const{
    vector<pair<GameActions,pair<float,float>>> ret_strategy;
    if(this->treeItem == NULL)return ret_strategy;


    shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();

    if(node->getType() == GameTreeNode::GameTreeNode::ACTION){
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(node);
        vector<GameActions>& gameActions = actionNode->getActions();
        int current_player = actionNode->getPlayer();

        vector<float> combos(gameActions.size(),0.0);
        vector<float> avg_strategy(gameActions.size(),0.0);
        float sum_strategy = 0;

        for(std::size_t index1 = 0;index1 < this->current_strategy.size() ;index1 ++){
            for(std::size_t index2 = 0;index2 < this->current_strategy.size() ;index2 ++){
                const vector<float>& one_strategy = this->current_strategy[index1][index2];
                if(one_strategy.empty())continue;

                const vector<vector<float>>& range = current_player == 0? this->p1_range:this->p2_range;
                if(range.size() <= index1 || range[index1].size() < index2) throw runtime_error(" index error when get range in tablestrategymodel");
                const float one_range = range[index1][index2];

                for(std::size_t i = 0;i < one_strategy.size(); i ++ ){
                    float one_prob = one_strategy[i];
                    combos[i] += one_prob * one_range;
                    avg_strategy[i] += one_prob * one_range;
                    sum_strategy += one_prob * one_range;
                }

                if(gameActions.size() != one_strategy.size()){
                    cout << "index: " << index1 << " " << index2 << endl;
                    cout << "size not match between gameAction and stragegy: " << gameActions.size() << " " << one_strategy.size() << endl;
                    throw runtime_error("size not match between gameAction and stragegy");
                }
            }
        }

        for(std::size_t i = 0;i < gameActions.size(); i ++ ){
            avg_strategy[i] = avg_strategy[i] / sum_strategy;
            pair<float,float> statics = pair<float,float>(combos[i],avg_strategy[i]);
            pair<GameActions,pair<float,float>> one_ret = pair<GameActions,pair<float,float>>(gameActions[i],statics);
            ret_strategy.push_back(one_ret);
        }
        return ret_strategy;
    }else{
        return ret_strategy;
    }


}

const vector<pair<GameActions,float>> TableStrategyModel::get_strategy(int i,int j) const{
    vector<pair<GameActions,float>> ret_strategy;
    if(this->treeItem == NULL) return ret_strategy;

    int strategy_number = this->ui_strategy_table[i][j].size();

    shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();

    if(node->getType() == GameTreeNode::GameTreeNode::ACTION){
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(node);

        vector<GameActions>& gameActions = actionNode->getActions();

        vector<float> strategies;

        // get range data - initally copied from paint_range - could probably be integrated in loops below for efficiancy
        vector<pair<int,int>> card_cords;
        const vector<vector<float>> *current_range;
        card_cords = ui_strategy_table[i][j];
        current_range = (0 == current_player ) ? & p1_range : & p2_range;

        if(p1_range.empty() || p2_range.empty()){
            return ret_strategy;
        }

        float range_number = 0;
        if(!card_cords.empty()){
            for(std::size_t indi = 0;indi < card_cords.size();indi ++){
                    range_number += (*current_range)[card_cords[indi].first][card_cords[indi].second];
            }
            range_number = range_number / card_cords.size();

            if(range_number < 0 || range_number > 1) throw runtime_error("range number incorrect in strategyitemdeletage");
        }
        else
            return ret_strategy;
        // got range data

        if(this->ui_strategy_table[i][j].size() > 0){
            strategies = vector<float>(gameActions.size());
            std::fill(strategies.begin(), strategies.end(), 0.);
        }
        for(std::pair<int,int> index:this->ui_strategy_table[i][j]){
            int index1 = index.first;
            int index2 = index.second;
            const vector<float>& one_strategy = this->current_strategy[index1][index2];
            if(gameActions.size() != one_strategy.size()){
                cout << "index: " << index1 << " " << index2 << endl;
                cout << "i,j: " << i << " " << j << endl;
                cout << "size not match between gameAction and stragegy: " << gameActions.size() << " " << one_strategy.size() << endl;
                throw runtime_error("size not match between gameAction and stragegy");
            }

            if ( range_number > 0)
                for(std::size_t indi = 0;indi < one_strategy.size();indi ++){
                    strategies[indi] += (one_strategy[indi] * (*current_range)[index1][index2] / range_number / strategy_number);
                }
        }

        for(std::size_t indi = 0;indi < strategies.size();indi ++){
            ret_strategy.push_back(std::pair<GameActions,float>(actionNode->getActions()[indi],
                                                                strategies[indi]));
        }

        return ret_strategy;
    }else{
        return ret_strategy;
    }

}

const vector<float> TableStrategyModel::get_ev_grid(int i,int j)const{
    vector<float> ret_evs;
    if(this->treeItem == NULL || this->current_evs.empty())return ret_evs;

    int strategy_number = this->ui_strategy_table[i][j].size();

    shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();

    if(node->getType() == GameTreeNode::GameTreeNode::ACTION){
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(node);

        vector<GameActions>& gameActions = actionNode->getActions();

//        vector<float> strategies;

//        if(this->ui_strategy_table[i][j].size() > 0){
//            strategies = vector<float>(gameActions.size());
//            std::fill(strategies.begin(), strategies.end(), 0.);
//        }
        for(std::pair<int,int> index:this->ui_strategy_table[i][j]){
            int index1 = index.first;
            int index2 = index.second;
            const vector<float>& one_strategy = this->current_strategy[index1][index2];
            const vector<float>& one_ev = this->current_evs[index1][index2];
            if(one_ev.size() != one_strategy.size()) return vector<float>();

            if(gameActions.size() != one_strategy.size()){
                cout << "index: " << index1 << " " << index2 << endl;
                cout << "i,j: " << i << " " << j << endl;
                cout << "size not match between gameAction and stragegy: " << gameActions.size() << " " << one_strategy.size() << endl;
                throw runtime_error("size not match between gameAction and stragegy");
            }
            float one_ev_float = 0;
            for(std::size_t indi = 0;indi < one_strategy.size();indi ++){
                one_ev_float += one_strategy[indi] * one_ev[indi];
            }
            ret_evs.push_back(one_ev_float);
        }

        return ret_evs;
    }else{
        return ret_evs;
    }
}


const vector<float> TableStrategyModel::get_strategies_evs(int i,int j)const{
    vector<float> ret_evs;
    if(this->treeItem == NULL || this->current_evs.empty())return ret_evs;

    const vector<vector<float>> *current_range;
    current_range = (0 == current_player ) ? & p1_range : & p2_range;
    if(p1_range.empty() || p2_range.empty()){
        return ret_evs;
    }

    shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();

    if(node->getType() == GameTreeNode::GameTreeNode::ACTION){
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(node);
        vector<GameActions>& gameActions = actionNode->getActions();

        vector<float> strategy_p;
        if(this->ui_strategy_table[i][j].size() > 0){
            ret_evs = vector<float>(gameActions.size());
            std::fill(ret_evs.begin(), ret_evs.end(), 0.);
            strategy_p = vector<float>(gameActions.size());
            std::fill(strategy_p.begin(), strategy_p.end(), 0.);
        }
        float range = 0;
        for(std::pair<int,int> index:this->ui_strategy_table[i][j]){
            int index1 = index.first;
            int index2 = index.second;
            const vector<float>& one_strategy = this->current_strategy[index1][index2];
            const vector<float>& one_ev = this->current_evs[index1][index2];
            const float one_range = (*current_range)[index1][index2];
            if(gameActions.size() != one_strategy.size() || one_ev.size() != one_strategy.size()){
                cout << "index: " << index1 << " " << index2 << endl;
                cout << "i,j: " << i << " " << j << endl;
                cout << "size not match between one_ev, gameAction and one_stragegy: "
                     << one_ev.size() << " " << gameActions.size() << " " << one_strategy.size() << endl;
                throw runtime_error("size not match between one_ev, gameAction and one_stragegy");
            }
            for(std::size_t indi = 0;indi < ret_evs.size();indi ++){
                ret_evs[indi] += one_strategy[indi] * one_ev[indi] * one_range;
                strategy_p[indi] += one_strategy[indi] * one_range;
            }
            range += one_range;
        }
        for(std::size_t indi = 0;indi < ret_evs.size();indi ++){
            if (strategy_p[indi] > 0. && range > 0.) {
                ret_evs[indi] = ret_evs[indi] / strategy_p[indi];
            }
        }
        return ret_evs;
    }else{
        return ret_evs;
    }
}

shared_ptr<Trainable> TableStrategyModel::get_current_trainable() const {
    if(this->treeItem == NULL) return nullptr;
    shared_ptr<GameTreeNode> node = this->treeItem->m_treedata.lock();
    if(node == nullptr || node->getType() != GameTreeNode::GameTreeNode::ACTION) return nullptr;
    
    if(this->qSolverJob->get_solver() == NULL || this->qSolverJob->get_solver()->get_solver() == NULL) return nullptr;

    shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(node);
    
    vector<Card> deal_cards;
    GameTreeNode::GameRound root_round = this->qSolverJob->get_solver()->getGameTree()->getRoot()->getRound();
    GameTreeNode::GameRound current_round = actionNode->getRound();
    
    if(root_round == GameTreeNode::GameRound::FLOP){
        if(current_round == GameTreeNode::GameRound::TURN){deal_cards.push_back(this->turn_card);}
        if(current_round == GameTreeNode::GameRound::RIVER){deal_cards.push_back(this->turn_card);deal_cards.push_back(this->river_card);}
    }
    else if(root_round == GameTreeNode::GameRound::TURN){
        if(current_round == GameTreeNode::GameRound::RIVER){deal_cards.push_back(this->river_card);}
    }
    
    return this->qSolverJob->get_solver()->get_solver()->get_trainable(actionNode, deal_cards);
}

QString TableStrategyModel::formatActionName(GameActions action, const shared_ptr<GameTreeNode>& node) const {
    if (!node) {
        return QString::fromStdString(action.toString());
    }

    GameTreeNode::PokerActions actType = action.getAction();
    if (actType != GameTreeNode::PokerActions::BET && actType != GameTreeNode::PokerActions::RAISE) {
        if (actType == GameTreeNode::PokerActions::CHECK) return tr("CHECK");
        if (actType == GameTreeNode::PokerActions::FOLD) return tr("FOLD");
        if (actType == GameTreeNode::PokerActions::CALL) {
            // Check if call is an All-in
            double round_start_pot = 0.0;
            shared_ptr<GameTreeNode> round_root = node;
            while (round_root->getParent() && round_root->getParent()->getRound() == node->getRound()) {
                round_root = round_root->getParent();
            }
            round_start_pot = round_root->getPot();

            double ip_street_commit = 0.0;
            double oop_street_commit = 0.0;
            shared_ptr<GameTreeNode> curr = node;
            shared_ptr<GameTreeNode> parent = curr->getParent();
            while (parent && parent->getRound() == node->getRound() && parent->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
                shared_ptr<ActionNode> action_parent = dynamic_pointer_cast<ActionNode>(parent);
                int p_player = action_parent->getPlayer();
                double action_amount = 0.0;
                bool found_action = false;
                for (size_t idx = 0; idx < action_parent->getChildrens().size(); ++idx) {
                    if (action_parent->getChildrens()[idx] == curr) {
                        GameActions act = action_parent->getActions()[idx];
                        if (act.getAction() == GameTreeNode::PokerActions::BET || act.getAction() == GameTreeNode::PokerActions::RAISE) {
                            action_amount = act.getAmount();
                            found_action = true;
                        }
                        break;
                    }
                }
                if (found_action) {
                    if (p_player == 0 && ip_street_commit == 0.0) {
                        ip_street_commit = action_amount;
                    } else if (p_player == 1 && oop_street_commit == 0.0) {
                        oop_street_commit = action_amount;
                    }
                }
                curr = parent;
                parent = curr->getParent();
            }

            int current_player = dynamic_pointer_cast<ActionNode>(node)->getPlayer();
            double opponent_street_commit = (current_player == 0 ? oop_street_commit : ip_street_commit);

            double total_commit_after_action = opponent_street_commit + round_start_pot / 2;
            if (this->qSolverJob && total_commit_after_action >= this->qSolverJob->stack - 0.01) {
                return tr("CALL (All-in)");
            }
            return tr("CALL");
        }
        return QString::fromStdString(action.toString());
    }

    double amount = action.getAmount();
    double round_start_pot = 0.0;
    shared_ptr<GameTreeNode> round_root = node;
    while (round_root->getParent() && round_root->getParent()->getRound() == node->getRound()) {
        round_root = round_root->getParent();
    }
    round_start_pot = round_root->getPot();

    double ip_street_commit = 0.0;
    double oop_street_commit = 0.0;
    shared_ptr<GameTreeNode> curr = node;
    shared_ptr<GameTreeNode> parent = curr->getParent();
    while (parent && parent->getRound() == node->getRound() && parent->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        shared_ptr<ActionNode> action_parent = dynamic_pointer_cast<ActionNode>(parent);
        int p_player = action_parent->getPlayer();
        double action_amount = 0.0;
        bool found_action = false;
        for (size_t idx = 0; idx < action_parent->getChildrens().size(); ++idx) {
            if (action_parent->getChildrens()[idx] == curr) {
                GameActions act = action_parent->getActions()[idx];
                if (act.getAction() == GameTreeNode::PokerActions::BET || act.getAction() == GameTreeNode::PokerActions::RAISE) {
                    action_amount = act.getAmount();
                    found_action = true;
                }
                break;
            }
        }
        if (found_action) {
            if (p_player == 0 && ip_street_commit == 0.0) {
                ip_street_commit = action_amount;
            } else if (p_player == 1 && oop_street_commit == 0.0) {
                oop_street_commit = action_amount;
            }
        }
        curr = parent;
        parent = curr->getParent();
    }

    int current_player = dynamic_pointer_cast<ActionNode>(node)->getPlayer();
    double player_street_commit_before = (current_player == 0 ? ip_street_commit : oop_street_commit);
    double opponent_street_commit = (current_player == 0 ? oop_street_commit : ip_street_commit);

    double pct = 0.0;
    if (actType == GameTreeNode::PokerActions::BET) {
        double pot_before = round_start_pot + ip_street_commit + oop_street_commit;
        if (pot_before > 0.01) {
            pct = amount / pot_before;
        }
    } else { // RAISE
        double call_amount = opponent_street_commit - player_street_commit_before;
        double pot_before = round_start_pot + ip_street_commit + oop_street_commit;
        double pot_called = pot_before + call_amount;
        double raise_amount = amount - opponent_street_commit;
        if (pot_called > 0.01) {
            pct = raise_amount / pot_called;
        }
    }

    double total_commit_after_action = amount + round_start_pot / 2;
    bool is_allin = false;
    if (this->qSolverJob && total_commit_after_action >= this->qSolverJob->stack - 0.01) {
        is_allin = true;
    }

    QString action_name_str = (actType == GameTreeNode::PokerActions::BET) ? tr("BET") : tr("RAISE");
    QString formatted_amount = QString::number(amount, 'f', 1);
    QString pct_str = QString("%1%").arg(qRound(pct * 100));

    if (is_allin) {
        if (pct > 0.01) {
            return QString("%1 %2 (%3, All-in)").arg(action_name_str, formatted_amount, pct_str);
        } else {
            return QString("%1 %2 (All-in)").arg(action_name_str, formatted_amount);
        }
    } else {
        return QString("%1 %2 (%3)").arg(action_name_str, formatted_amount, pct_str);
    }
}
