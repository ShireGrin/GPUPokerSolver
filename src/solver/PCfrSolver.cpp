//
// Created by Xuefeng Huang on 2020/1/31.
//

#include <include/solver/BestResponse.h>
#include "include/solver/PCfrSolver.h"
#include <QtCore>
#include <QObject>
#include <QTranslator>
#include "include/tools/dbmanager.h"

//#define DEBUG;

PCfrSolver::~PCfrSolver(){
    //cout << "Pcfr destroyed" << endl;
}

PCfrSolver::PCfrSolver(
        shared_ptr<GameTree> tree, vector<PrivateCards> range1, vector<PrivateCards> range2,
        vector<int> initial_board, shared_ptr<Compairer> compairer, Deck deck, int iteration_number, bool debug,
        int print_interval, string logfile, string trainer, Solver::MonteCarolAlg monteCarolAlg,int warmup, float accuracy, bool use_isomorphism, int use_halffloats,int num_threads
        ) : Solver(tree)
{
    this->initial_board = initial_board;
    this->initial_board_long = Card::boardInts2long(initial_board);
    this->logfile = logfile;
    this->trainer = trainer;
    this->warmup = warmup;

    range1 = this->noDuplicateRange(range1,initial_board_long);
    range2 = this->noDuplicateRange(range2,initial_board_long);

    this->range1 = range1;
    this->range2 = range2;
    this->player_number = 2;
    this->ranges = vector<vector<PrivateCards>>(this->player_number);
    this->ranges[0] = range1;
    this->ranges[1] = range2;

    this->compairer = compairer;

    this->deck = deck;
    this->use_isomorphism = use_isomorphism;
    this->use_halffloats = use_halffloats;

    this->rrm = RiverRangeManager(compairer);
    this->iteration_number = iteration_number;

    vector<vector<PrivateCards>> private_cards(this->player_number);
    private_cards[0] = range1;
    private_cards[1] = range2;
    pcm = PrivateCardsManager(private_cards,this->player_number,Card::boardInts2long(this->initial_board));
    this->debug = debug;
    this->print_interval = print_interval;
    this->monteCarolAlg = monteCarolAlg;
    this->accuracy = accuracy;
    if(num_threads == -1){
        num_threads = omp_get_num_procs();
    }
    qDebug().noquote() << QString::fromStdString(tfm::format(QObject::tr("Using %s threads").toStdString().c_str(),num_threads));
    this->num_threads = num_threads;
    this->distributing_task = false;
    omp_set_num_threads(this->num_threads);
    setTrainable(this->tree->getRoot());
    this->root_round = this->tree->getRoot()->getRound();
    if(this->root_round == GameTreeNode::GameRound::PREFLOP){
        this->split_round = GameTreeNode::GameRound::FLOP;
    }else if(this->root_round == GameTreeNode::GameRound::FLOP){
        this->split_round = GameTreeNode::GameRound::TURN;
    }else if(this->root_round == GameTreeNode::GameRound::TURN){
        this->split_round = GameTreeNode::GameRound::RIVER;
    }else{
        // do not use multithread in river, really not necessary
        this->split_round = GameTreeNode::GameRound::PREFLOP;
    }
}

const vector<PrivateCards> &PCfrSolver::playerHands(int player) {
    if(player == 0){
        return range1;
    }else if (player == 1){
        return range2;
    }else{
        throw runtime_error("player not found");
    }
}

vector<vector<float>> PCfrSolver::getReachProbs() {
    vector<vector<float>> retval(this->player_number);
    for(int player = 0;player < this->player_number;player ++){
        vector<PrivateCards> player_cards = this->playerHands(player);
        vector<float> reach_prob(player_cards.size());
        for(std::size_t i = 0;i < player_cards.size();i ++){
            reach_prob[i] = player_cards[i].weight;
        }
        retval[player] = reach_prob;
    }
    return retval;
}

vector<PrivateCards>
PCfrSolver::noDuplicateRange(const vector<PrivateCards> &private_range, uint64_t board_long) {
    vector<PrivateCards> range_array;
    unordered_map<int,bool> rangekv;
    for(PrivateCards one_range:private_range){
        if(rangekv.find(one_range.hashCode()) != rangekv.end())
            throw runtime_error(tfm::format("duplicated key %s",one_range.toString()));
        rangekv[one_range.hashCode()] = true;
        uint64_t hand_long = Card::boardInts2long(one_range.get_hands());
        if(!Card::boardsHasIntercept(hand_long,board_long)){
            range_array.push_back(one_range);
        }
    }
    return range_array;

}

void PCfrSolver::setTrainable(const shared_ptr<GameTreeNode>& root) {
    if(root->getType() == GameTreeNode::ACTION){
        shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(root);

        int player = action_node->getPlayer();

        if(this->trainer == "cfr_plus"){
            //vector<PrivateCards> player_privates = this->ranges[player];
            //action_node->setTrainable(make_shared<CfrPlusTrainable>(action_node,player_privates));
            throw runtime_error(tfm::format("trainer %s not supported",this->trainer));
        }else if(this->trainer == "discounted_cfr"){
            vector<PrivateCards>* player_privates = &this->ranges[player];
            //action_node->setTrainable(make_shared<DiscountedCfrTrainable>(action_node,player_privates));
            int num;
            GameTreeNode::GameRound gr = this->tree->getRoot()->getRound();
            int root_round = GameTreeNode::gameRound2int(gr);
            int current_round = GameTreeNode::gameRound2int(root->getRound());
            int gap = current_round - root_round;

            if(gap == 2) {
                num = this->deck.getCards().size() * this->deck.getCards().size() +
                    this->deck.getCards().size() + 1;
            }else if(gap == 1) {
                num = this->deck.getCards().size() + 1;
            }else if(gap == 0) {
                num =  1;
            }else{
                throw runtime_error("gap not understand");
            }
            action_node->setTrainable(vector<shared_ptr<Trainable>>(num),player_privates);
        }else{
            throw runtime_error(tfm::format("trainer %s not found",this->trainer));
        }

        vector<shared_ptr<GameTreeNode>> childrens =  action_node->getChildrens();
        for(shared_ptr<GameTreeNode> one_child:childrens) setTrainable(one_child);
    }else if(root->getType() == GameTreeNode::CHANCE) {
        shared_ptr<ChanceNode> chance_node = std::dynamic_pointer_cast<ChanceNode>(root);
        shared_ptr<GameTreeNode> children = chance_node->getChildren();
        setTrainable(children);
    }
    else if(root->getType() == GameTreeNode::TERMINAL){

    }else if(root->getType() == GameTreeNode::SHOWDOWN){

    }
}

vector<int> PCfrSolver::getAllAbstractionDeal(int deal){
    vector<int> all_deal;
    int card_num = this->deck.getCards().size();
    if(deal == 0){
        all_deal.push_back(deal);
    } else if (deal > 0 && deal <= card_num){
        int origin_deal = int((deal - 1) / 4) * 4;
        for(int i = 0;i < 4;i ++){
            int one_card = origin_deal + i + 1;

            Card *first_card = const_cast<Card *>(&(this->deck.getCards()[origin_deal + i]));
            uint64_t first_long = Card::boardInt2long(
                    first_card->getCardInt());
            if (Card::boardsHasIntercept(first_long, this->initial_board_long))continue;
            all_deal.push_back(one_card);
        }
    } else{
        //cout << "______________________" << endl;
        int c_deal = deal - (1 + card_num);
        int first_deal = int((c_deal / card_num) / 4) * 4;
        int second_deal = int((c_deal % card_num) / 4) * 4;

        for(int i = 0;i < 4;i ++) {
            for(int j = 0;j < 4;j ++) {
                if(first_deal == second_deal && i == j) continue;

                Card *first_card = const_cast<Card *>(&(this->deck.getCards()[first_deal + i]));
                uint64_t first_long = Card::boardInt2long(
                        first_card->getCardInt());
                if (Card::boardsHasIntercept(first_long, this->initial_board_long))continue;

                Card *second_card = const_cast<Card *>(&(this->deck.getCards()[second_deal + j]));
                uint64_t second_long = Card::boardInt2long(
                        second_card->getCardInt());
                if (Card::boardsHasIntercept(second_long, this->initial_board_long))continue;

                int one_card = card_num * (first_deal + i) + (second_deal + j) + 1 + card_num;
                //cout << ";" << this->deck.getCards()[first_deal + i].toString() << "," << this->deck.getCards()[second_deal + j].toString();
                all_deal.push_back(one_card);
            }
        }
        //cout << endl;
    }
    return all_deal;
}

vector<float> PCfrSolver::cfr(int player, const shared_ptr<GameTreeNode>& node, const vector<float> &reach_probs, int iter,
        uint64_t current_board,int deal) {
    switch(node->getType()) {
        case GameTreeNode::ACTION: {
                                       shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(node);
                                       return actionUtility(player, action_node, reach_probs, iter, current_board,deal);
                                   }case GameTreeNode::SHOWDOWN: {
                                       shared_ptr<ShowdownNode> showdown_node = std::dynamic_pointer_cast<ShowdownNode>(node);
                                       return showdownUtility(player, showdown_node, reach_probs, iter, current_board,deal);
                                   }case GameTreeNode::TERMINAL: {
                                       shared_ptr<TerminalNode> terminal_node = std::dynamic_pointer_cast<TerminalNode>(node);
                                       return terminalUtility(player, terminal_node, reach_probs, iter, current_board,deal);
                                   }case GameTreeNode::CHANCE: {
                                       shared_ptr<ChanceNode> chance_node = std::dynamic_pointer_cast<ChanceNode>(node);
                                       return chanceUtility(player, chance_node, reach_probs, iter, current_board,deal);
                                   }default:
                                   throw runtime_error("node type unknown");
    }
}

vector<float>
PCfrSolver::chanceUtility(int player, const shared_ptr<ChanceNode>& node, const vector<float> &reach_probs, int iter,
        uint64_t current_board,int deal) {
    vector<Card>& cards = this->deck.getCards();
    //float[] cardWeights = getCardsWeights(player,reach_probs[1 - player],current_board);

    int card_num = node->getCards().size();
    if(card_num % 4 != 0) throw runtime_error("card num cannot round 4");
    // 可能的发牌情况,2代表每个人的holecard是两张
    int possible_deals = node->getCards().size() - Card::long2board(current_board).size() - 2;
    int oppo = 1 - player;

    //vector<float> chance_utility(reach_probs[player].size());
    vector<float> chance_utility = vector<float>(this->ranges[player].size());
    fill(chance_utility.begin(),chance_utility.end(),0);

    int random_deal = 0;
    if(this->monteCarolAlg==MonteCarolAlg::PUBLIC) {
        if (this->round_deal[GameTreeNode::gameRound2int(node->getRound())] == -1) {
            random_deal = random(1, possible_deals + 1 + 2);
            this->round_deal[GameTreeNode::gameRound2int(node->getRound())] = random_deal;
        } else {
            random_deal = this->round_deal[GameTreeNode::gameRound2int(node->getRound())];
        }
    }
    //vector<vector<vector<float>>> arr_new_reach_probs = vector<vector<vector<float>>>(node->getCards().size());

    vector<vector<float>> results(node->getCards().size());
    //fill(results.begin(),results.end(),nullptr);

    vector<float> multiplier;
    if(iter <= this->warmup){
        multiplier = vector<float>(card_num);
        fill(multiplier.begin(), multiplier.end(), 0);
        for (int card_base = 0; card_base < card_num / 4; card_base++) {
            int cardr = std::rand() % 4;
            int card_target = card_base * 4 + cardr;
            int multiplier_num = 0;
            for (int i = 0; i < 4; i++) {
                int i_card = card_base * 4 + i;
                if (i == cardr) {
                    Card *one_card = const_cast<Card *>(&(node->getCards()[i_card]));
                    uint64_t card_long = Card::boardInt2long(
                            one_card->getCardInt());
                    if (!Card::boardsHasIntercept(card_long, current_board)) {
                        multiplier_num += 1;
                    }
                } else {
                    Card *one_card = const_cast<Card *>(&(node->getCards()[i_card]));
                    uint64_t card_long = Card::boardInt2long(
                            one_card->getCardInt());
                    if (!Card::boardsHasIntercept(card_long, current_board)) {
                        multiplier_num += 1;
                    }
                }
            }
            multiplier[card_target] = multiplier_num;
        }
    }

    vector<int> valid_cards;
    valid_cards.reserve(node->getCards().size());

    for(std::size_t card = 0;card < node->getCards().size();card ++) {
        shared_ptr<GameTreeNode> one_child = node->getChildren();
        Card *one_card = const_cast<Card *>(&(node->getCards()[card]));
        uint64_t card_long = Card::boardInt2long(one_card->getCardInt());//Card::boardCards2long(new Card[]{one_card});
        if (Card::boardsHasIntercept(card_long, current_board)) continue;
        if (iter <= this->warmup && multiplier[card] == 0) continue;
        if (this->color_iso_offset[deal][one_card->getCardInt() % 4] < 0) continue;
        valid_cards.push_back(card);
    }

#pragma omp parallel for schedule(static)
    for(std::size_t valid_ind = 0;valid_ind < valid_cards.size();valid_ind++) {
        int card = valid_cards[valid_ind];
        shared_ptr<GameTreeNode> one_child = node->getChildren();
        Card *one_card = const_cast<Card *>(&(node->getCards()[card]));
        uint64_t card_long = Card::boardInt2long(one_card->getCardInt());//Card::boardCards2long(new Card[]{one_card});

        uint64_t new_board_long = current_board | card_long;
        if (this->monteCarolAlg == MonteCarolAlg::PUBLIC) {
            throw runtime_error("parallel solver don't support public monte carol");
        }

        //cout << "Card deal:" << one_card->toString() << endl;

        vector<PrivateCards> &playerPrivateCard = (this->ranges[player]);
        vector<PrivateCards> &oppoPrivateCards = (this->ranges[1 - player]);


        vector<float> new_reach_probs = vector<float>(oppoPrivateCards.size());



#ifdef DEBUG
        if (playerPrivateCard.size() != this->ranges[player].size()) throw runtime_error("length not match");
        if (oppoPrivateCards.size() != this->ranges[1 - player].size()) throw runtime_error("length not match");
#endif

        int player_hand_len = this->ranges[oppo].size();
        for (int player_hand = 0; player_hand < player_hand_len; player_hand++) {
            PrivateCards &one_private = this->ranges[oppo][player_hand];
            uint64_t privateBoardLong = one_private.toBoardLong();
            if (Card::boardsHasIntercept(card_long, privateBoardLong)) {
                new_reach_probs[player_hand] = 0;
                continue;
            }
            new_reach_probs[player_hand] = reach_probs[player_hand] / possible_deals;
        }
#ifdef DEBUG
        if (Card::boardsHasIntercept(current_board, card_long))
            throw runtime_error("board has intercept with dealt card");
#endif

        int new_deal;
        if(deal == 0){
            new_deal = card + 1;
        } else if (deal > 0 && deal <= card_num){
            int origin_deal = deal - 1;

#ifdef DEBUG
            if(origin_deal == card) throw runtime_error("deal should not be equal");
#endif
            new_deal = card_num * origin_deal + card;
            new_deal += (1 + card_num);
        } else{
            throw runtime_error(tfm::format("deal out of range : %s ",deal));
        }
        if(this->distributing_task && node->getRound() == this->split_round) {
            results[one_card->getNumberInDeckInt()] = vector<float>(this->ranges[player].size());
            //TaskParams taskParams = TaskParams();
        }else {
            vector<float> child_utility = this->cfr(player, one_child, new_reach_probs, iter, new_board_long, new_deal);
            results[one_card->getNumberInDeckInt()] = child_utility;
        }
    }

    for(std::size_t card = 0;card < node->getCards().size();card ++) {
        Card *one_card = const_cast<Card *>(&(node->getCards()[card]));
        vector<float> child_utility;
        int offset = this->color_iso_offset[deal][one_card->getCardInt() % 4];
        if(offset < 0) {
            int rank1 = one_card->getCardInt() % 4;
            int rank2 = rank1 + offset;
#ifdef DEBUG
            if(rank2 < 0) throw runtime_error("rank error");
#endif
            child_utility = results[one_card->getNumberInDeckInt() + offset];
            exchange_color(child_utility,this->pcm.getPreflopCards(player),rank1,rank2);
        }else{
            child_utility = results[one_card->getNumberInDeckInt()];
        }
        if(child_utility.empty())
            continue;

#ifdef DEBUG
        if(child_utility.size() != chance_utility.size()) throw runtime_error("length not match");
#endif
        if(iter > this->warmup) {
            for (std::size_t i = 0; i < child_utility.size(); i++)
                chance_utility[i] += child_utility[i];
        }else{
            for (std::size_t i = 0; i < child_utility.size(); i++)
                chance_utility[i] += child_utility[i] * multiplier[card];
        }
    }

#ifdef DEBUG
    if(this->monteCarolAlg == MonteCarolAlg::PUBLIC) {
        throw runtime_error("not possible");
    }
    if(chance_utility.size() != this->ranges[player].size()) {
        throw runtime_error("size problems");
    }
#endif
    return chance_utility;
}

vector<float>
PCfrSolver::actionUtility(int player, const shared_ptr<ActionNode>& node, const vector<float> &reach_probs, int iter,
        uint64_t current_board,int deal) {
    int oppo = 1 - player;
    const vector<PrivateCards>& node_player_private_cards = this->ranges[node->getPlayer()];

    vector<float> payoffs = vector<float>(this->ranges[player].size());
    fill(payoffs.begin(),payoffs.end(),0);
    vector<shared_ptr<GameTreeNode>>& children =  node->getChildrens();
    vector<GameActions>& actions =  node->getActions();

    shared_ptr<Trainable> trainable;

    /*
       if(iter <= this->warmup){
       vector<int> deals = this->getAllAbstractionDeal(deal);
       trainable = node->getTrainable(deals[0]);
       }else{
       trainable = node->getTrainable(deal);
       }
       */
    trainable = node->getTrainable(deal,true,this->use_halffloats);

#ifdef DEBUG
    if(trainable == nullptr){
        throw runtime_error("null trainable");
    }
#endif

    const vector<float> current_strategy = use_average_strategy_for_cfr ? trainable->getAverageStrategy() : trainable->getcurrentStrategy();
#ifdef DEBUG
    if (current_strategy.size() != actions.size() * node_player_private_cards.size()) {
        node->printHistory();
        throw runtime_error(tfm::format(
                    "length not match %s - %s \n action size %s private_card size %s"
                    ,current_strategy.size()
                    ,actions.size() * node_player_private_cards.size()
                    ,actions.size()
                    ,node_player_private_cards.size()
                    ));
    }
#endif

    //为了节省计算成本将action regret 存在一位数组而不是二维数组中，两个纬度分别是（该infoset有多少动作,该palyer有多少holecard）
    vector<float> regrets(actions.size() * node_player_private_cards.size());

    vector<vector<float>> all_action_utility(actions.size());
    int node_player = node->getPlayer();

    vector<vector<float>> results(actions.size());
    for (std::size_t action_id = 0; action_id < actions.size(); action_id++) {

        if (node_player != player) {
            vector<float> new_reach_prob = vector<float>(reach_probs.size());
            for (std::size_t hand_id = 0; hand_id < new_reach_prob.size(); hand_id++) {
                float strategy_prob = current_strategy[hand_id + action_id * node_player_private_cards.size()];
                new_reach_prob[hand_id] = reach_probs[hand_id] * strategy_prob;
            }
            //#pragma omp task shared(results,action_id)
            results[action_id] = this->cfr(player, children[action_id], new_reach_prob, iter,
                    current_board,deal);
        }else {
            //#pragma omp task shared(results,action_id)
            results[action_id] = this->cfr(player, children[action_id], reach_probs, iter,
                    current_board,deal);
        }

    }

    //#pragma omp taskwait
    for (std::size_t action_id = 0; action_id < actions.size(); action_id++) {
        vector<float> action_utilities = results[action_id];
        if(action_utilities.empty()){
            continue;
        }
        all_action_utility[action_id] = action_utilities;

        // cfr结果是每手牌的收益，payoffs代表的也是每手牌的收益，他们的长度理应相等
#ifdef DEBUG
        if (action_utilities.size() != payoffs.size()) {
            cout << ("errmsg") << endl;
            cout << (tfm::format("node player %s ", node->getPlayer())) << endl;
            node->printHistory();
            throw runtime_error(
                    tfm::format(
                        "action and payoff length not match %s - %s", action_utilities.size(),
                        payoffs.size()
                        )
                    );
        }
#endif

        for (std::size_t hand_id = 0; hand_id < action_utilities.size(); hand_id++) {
            if (player == node->getPlayer()) {
                float strategy_prob = current_strategy[hand_id + action_id * node_player_private_cards.size()];
                payoffs[hand_id] += strategy_prob * (action_utilities)[hand_id];
            } else {
                payoffs[hand_id] += (action_utilities)[hand_id];
            }
        }
    }


    if (player == node->getPlayer()) {
        for (std::size_t i = 0; i < node_player_private_cards.size(); i++) {
            //boolean regrets_all_negative = true;
            for (std::size_t action_id = 0; action_id < actions.size(); action_id++) {
                // 下面是regret计算的伪代码
                // regret[action_id * player_hc: (action_id + 1) * player_hc]
                //     = all_action_utilitiy[action_id] - payoff[action_id]
                regrets[action_id * node_player_private_cards.size() + i] =
                    (all_action_utility[action_id])[i] - payoffs[i];
            }
        }

        if(!this->distributing_task && !this->collecting_statics) {
            if (iter > this->warmup) {
                trainable->updateRegrets(regrets, iter + 1, reach_probs);
            }/*else if(iter < this->warmup){
               vector<int> deals = this->getAllAbstractionDeal(deal);
               shared_ptr<Trainable> one_trainable = node->getTrainable(deals[0]);
               one_trainable->updateRegrets(regrets, iter + 1, reach_probs[player]);
               }*/
            else {
                // iter == this->warmup
                vector<int> deals = this->getAllAbstractionDeal(deal);
                shared_ptr<Trainable> standard_trainable = nullptr;
                for (int one_deal : deals) {
                    shared_ptr<Trainable> one_trainable = node->getTrainable(one_deal,true,this->use_halffloats);
                    if (standard_trainable == nullptr) {
                        one_trainable->updateRegrets(regrets, iter + 1, reach_probs);
                        standard_trainable = one_trainable;
                    } else {
                        one_trainable->copyStrategy(standard_trainable);
                    }
                }
            }
        }

        if(this->collecting_statics || (iter % this->print_interval == 0)){
            float oppo_sum = 0;
            vector<float> oppo_card_sum = vector<float> (52);
            fill(oppo_card_sum.begin(),oppo_card_sum.end(),0);

            const vector<PrivateCards>& oppo_hand = playerHands(oppo);
            for(std::size_t i = 0;i < oppo_hand.size();i ++){
                oppo_card_sum[oppo_hand[i].card1] += reach_probs[i];
                oppo_card_sum[oppo_hand[i].card2] += reach_probs[i];
                oppo_sum += reach_probs[i];
            }

            const vector<PrivateCards>& player_hand = playerHands(player);
            vector<float> evs(actions.size() * node_player_private_cards.size(),0.0);
            for (std::size_t action_id = 0; action_id < actions.size(); action_id++) {
                for (std::size_t hand_id = 0; hand_id < node_player_private_cards.size(); hand_id++) {
                    float one_ev = (all_action_utility)[action_id][hand_id];//current_strategy; //[hand_id + action_id * node_player_private_cards.size()];

                    int oppo_same_card_ind = this->pcm.indPlayer2Player(player,oppo,hand_id);
                    float plus_reach_prob;

                    const PrivateCards& one_player_hand = player_hand[hand_id];
                    if(oppo_same_card_ind == -1){
                        plus_reach_prob = 0;
                    }else{
                        plus_reach_prob = reach_probs[oppo_same_card_ind];
                    }

                    float rp_sum = (
                            oppo_sum - oppo_card_sum[one_player_hand.card1]
                            - oppo_card_sum[one_player_hand.card2]
                            + plus_reach_prob);

                    evs[hand_id + action_id * node_player_private_cards.size()] = one_ev / rp_sum;
                }
            }
            // TODO if evs contains nan what should we do?
            trainable->setEv(evs);
        }

    }
    return payoffs;

}

vector<float>
PCfrSolver::showdownUtility(int player, const shared_ptr<ShowdownNode>& node, const vector<float> &reach_probs,
        int iter, uint64_t current_board,int deal) {
    // player win时候player的收益，player lose的时候收益明显为-player_payoff
    int oppo = 1 - player;
    float win_payoff = node->get_payoffs(ShowdownNode::ShowDownResult::NOTTIE,player,player);
    float lose_payoff = node->get_payoffs(ShowdownNode::ShowDownResult::NOTTIE,oppo,player);
    const vector<PrivateCards>& player_private_cards = this->ranges[player];
    const vector<PrivateCards>& oppo_private_cards = this->ranges[oppo];

    const vector<RiverCombs>& player_combs = this->rrm.getRiverCombos(player,player_private_cards,current_board);
    const vector<RiverCombs>& oppo_combs = this->rrm.getRiverCombos(oppo,oppo_private_cards,current_board);

    vector<float> payoffs = vector<float>(player_private_cards.size());

    float winsum = 0;
    vector<float> card_winsum = vector<float> (52);//node->card_sum;
    fill(card_winsum.begin(),card_winsum.end(),0);

    int j = 0;
    for(std::size_t i = 0;i < player_combs.size();i ++){
        const RiverCombs& one_player_comb = player_combs[i];
        while (j < oppo_combs.size() && one_player_comb.rank < oppo_combs[j].rank){
            const RiverCombs& one_oppo_comb = oppo_combs[j];
            winsum += reach_probs[one_oppo_comb.reach_prob_index];
            card_winsum[one_oppo_comb.private_cards.card1] += reach_probs[one_oppo_comb.reach_prob_index];
            card_winsum[one_oppo_comb.private_cards.card2] += reach_probs[one_oppo_comb.reach_prob_index];
            j ++;
        }
        payoffs[one_player_comb.reach_prob_index] = (winsum
                - card_winsum[one_player_comb.private_cards.card1]
                - card_winsum[one_player_comb.private_cards.card2]
                ) * win_payoff;
    }

    // 计算失败时的payoff
    float losssum = 0;
    vector<float>& card_losssum = card_winsum;
    fill(card_losssum.begin(),card_losssum.end(),0);

    j = oppo_combs.size() - 1;
    for(int i = player_combs.size() - 1;i >= 0;i --){
        const RiverCombs& one_player_comb = player_combs[i];
        while (j >= 0 && one_player_comb.rank > oppo_combs[j].rank){
            const RiverCombs& one_oppo_comb = oppo_combs[j];
            losssum += reach_probs[one_oppo_comb.reach_prob_index];
            card_losssum[one_oppo_comb.private_cards.card1] += reach_probs[one_oppo_comb.reach_prob_index];
            card_losssum[one_oppo_comb.private_cards.card2] += reach_probs[one_oppo_comb.reach_prob_index];
            j --;
        }
        payoffs[one_player_comb.reach_prob_index] += (losssum
                - card_losssum[one_player_comb.private_cards.card1]
                - card_losssum[one_player_comb.private_cards.card2]
                ) * lose_payoff;
    }
    return payoffs;
}

vector<float>
PCfrSolver::terminalUtility(int player, const shared_ptr<TerminalNode>& node, const vector<float> &reach_prob, int iter,
        uint64_t current_board,int deal) {
    float player_payoff = node->get_payoffs()[player];

    int oppo = 1 - player;
    const vector<PrivateCards>& player_hand = playerHands(player);
    const vector<PrivateCards>& oppo_hand = playerHands(oppo);

    vector<float> payoffs = vector<float>(this->playerHands(player).size());

    float oppo_sum = 0;
    vector<float> oppo_card_sum = vector<float> (52);
    fill(oppo_card_sum.begin(),oppo_card_sum.end(),0);

    for(std::size_t i = 0;i < oppo_hand.size();i ++){
        oppo_card_sum[oppo_hand[i].card1] += reach_prob[i];
        oppo_card_sum[oppo_hand[i].card2] += reach_prob[i];
        oppo_sum += reach_prob[i];
    }

    for(std::size_t i = 0;i < player_hand.size();i ++){
        const PrivateCards& one_player_hand = player_hand[i];
        if(Card::boardsHasIntercept(current_board,Card::boardInts2long(one_player_hand.get_hands()))){
            continue;
        }
        int oppo_same_card_ind = this->pcm.indPlayer2Player(player,oppo,i);
        float plus_reach_prob;
        if(oppo_same_card_ind == -1){
            plus_reach_prob = 0;
        }else{
            plus_reach_prob = reach_prob[oppo_same_card_ind];
        }
        payoffs[i] = player_payoff * (
                oppo_sum - oppo_card_sum[one_player_hand.card1]
                - oppo_card_sum[one_player_hand.card2]
                + plus_reach_prob
                );
    }

    return payoffs;
}

void PCfrSolver::findGameSpecificIsomorphisms() {
    // hand isomorphisms
    vector<Card> board_cards = Card::long2boardCards(this->initial_board_long);
    for(int i = 0;i <= 1;i ++){
        vector<PrivateCards>& range = i == 0?this->range1:this->range2;
        for(std::size_t i_range = 0;i_range < range.size();i_range ++) {
            PrivateCards one_range = range[i_range];
            uint32_t range_hash[4]; // four colors, hash of the isomorphisms range + hand combos
            for(int i = 0;i < 4;i ++)range_hash[i] = 0;
            for (int color = 0; color < 4; color++) {
                for (Card one_card:board_cards) {
                    if (one_card.getCardInt() % 4 == color) {
                        range_hash[color] = range_hash[color] | (1 << (one_card.getCardInt() / 4));
                    }
                }
            }
            for (int color = 0; color < 4; color++) {
                for (int one_card_int:{one_range.card1,one_range.card2}) {
                    if (one_card_int % 4 == color) {
                        range_hash[color] = range_hash[color] | (1 << (one_card_int / 4 + 16));
                    }
                }
            }
            // TODO check whethe hash is equal with others
        }
    }

    // chance node isomorphisms
    uint16_t color_hash[4];
    for(int i = 0;i < 4;i ++)color_hash[i] = 0;
    for (Card one_card:board_cards) {
        int rankind = one_card.getCardInt() % 4;
        int suitind = one_card.getCardInt() / 4;
        color_hash[rankind] = color_hash[rankind] | (1 << suitind);
    }
    for(int i = 0;i < 4;i ++){
        this->color_iso_offset[0][i] = 0;
        for(int j = 0;j < i;j ++){
            if(color_hash[i] == color_hash[j]){
                this->color_iso_offset[0][i] = j - i;
                continue;
            }
        }
    }
    for(std::size_t deal = 0;deal < this->deck.getCards().size();deal ++) {
        uint16_t color_hash[4];
        for(int i = 0;i < 4;i ++)color_hash[i] = 0;
        // chance node isomorphisms
        for (Card one_card:board_cards) {
            int rankind = one_card.getCardInt() % 4;
            int suitind = one_card.getCardInt() / 4;
            color_hash[rankind] = color_hash[rankind] | (1 << suitind);
        }
        Card one_card = this->deck.getCards()[deal];
        int rankind = one_card.getCardInt() % 4;
        int suitind = one_card.getCardInt() / 4;
        color_hash[rankind] = color_hash[rankind] | (1 << suitind);
        for (int i = 0; i < 4; i++) {
            this->color_iso_offset[deal + 1][i] = 0;
            for (int j = 0; j < i; j++) {
                if (color_hash[i] == color_hash[j]) {
                    this->color_iso_offset[deal + 1][i] = j - i;
                    continue;
                }
            }
        }
    }
}

void PCfrSolver::purnTree() {
    // TODO how to purn the tree, use wramup to start training in memory-save mode, and switch to purn tree directly to both save memory and speedup
}

void PCfrSolver::stop() {
    this->nowstop = true;
}

void PCfrSolver::train() {

    vector<vector<PrivateCards>> player_privates(this->player_number);
    player_privates[0] = pcm.getPreflopCards(0);
    player_privates[1] = pcm.getPreflopCards(1);
    if(this->use_isomorphism){
        this->findGameSpecificIsomorphisms();
    }

    BestResponse br = BestResponse(player_privates,this->player_number,this->pcm,this->rrm,this->deck,this->debug,this->color_iso_offset,this->split_round,this->num_threads,this->use_halffloats);

    this->last_exploitability = br.printExploitability(tree->getRoot(), 0, tree->getRoot()->getPot(), initial_board_long);
    this->last_iteration = 0;

    vector<vector<float>> reach_probs = this->getReachProbs();
    ofstream fileWriter;
    if(!this->logfile.empty())fileWriter.open(this->logfile);

    uint64_t begintime = timeSinceEpochMillisec();
    uint64_t endtime = timeSinceEpochMillisec();

    for(int i = 0;i < this->iteration_number;i++){
        for(int player_id = 0;player_id < this->player_number;player_id ++) {
            this->round_deal = vector<int>{-1,-1,-1,-1};
            //#pragma omp parallel
            {
                //#pragma omp single
                {
                    //this->distributing_task = true;
                    cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id], i, this->initial_board_long,0);
                    //throw runtime_error("returning...");
                }
            }
        }
        if( (i % this->print_interval == 0 && i != 0 && i >= this->warmup) || this->nowstop) {
            endtime = timeSinceEpochMillisec();
            long time_ms = endtime - begintime;
            qDebug().noquote() << "-------------------";
            float expliotibility = br.printExploitability(tree->getRoot(), i + 1, tree->getRoot()->getPot(), initial_board_long);
            this->last_exploitability = expliotibility;
            this->last_iteration = i + 1;
            float avg_speed_ms = float(time_ms) / (i + 1);
            qDebug().noquote() << QObject::tr("time used: ") << float(time_ms) / 1000 << QObject::tr(" second.")
                << QObject::tr(" (avg speed: ") << avg_speed_ms << QObject::tr(" ms/iteration).");

            auto formatTime = [](float seconds) -> QString {
                if (seconds < 0.0f) return "N/A";
                if (seconds < 60.0f) {
                    return QString("%1s").arg(seconds, 0, 'f', 1);
                }
                int minutes = static_cast<int>(seconds) / 60;
                int remaining_seconds = static_cast<int>(seconds) % 60;
                if (minutes < 60) {
                    return QString("%1m %2s").arg(minutes).arg(remaining_seconds);
                }
                int hours = minutes / 60;
                int remaining_minutes = minutes % 60;
                return QString("%1h %2m %3s").arg(hours).arg(remaining_minutes).arg(remaining_seconds);
            };

            int remaining_iters = this->iteration_number - (i + 1);
            float remaining_sec_by_iter = (remaining_iters > 0) ? (avg_speed_ms * remaining_iters / 1000.0f) : 0.0f;
            QString iter_est = formatTime(remaining_sec_by_iter);

            QString acc_est = "reached";
            if (this->accuracy > 0.0f && expliotibility > this->accuracy) {
                float target_iters = (i + 1) * (expliotibility / this->accuracy);
                float remaining_iters_to_accuracy = target_iters - (i + 1);
                float remaining_sec_by_accuracy = avg_speed_ms * remaining_iters_to_accuracy / 1000.0f;
                acc_est = formatTime(remaining_sec_by_accuracy);
            }

            qDebug().noquote() << QObject::tr("Estimated remaining time:")
                << QObject::tr("to max iterations: ") << iter_est
                << QObject::tr("| to target accuracy (assuming 1/t convergence): ") << acc_est;
            if(!this->logfile.empty()){
                json jo;
                jo["iteration"] = i;
                jo["exploitibility"] = expliotibility;
                jo["time_ms"] = time_ms;
                fileWriter << jo << endl;
            }
            if(expliotibility <= this->accuracy){
                break;
            }
            if(this->nowstop){
                this->nowstop = false;
                break;
            }
            //begintime = timeSinceEpochMillisec();
        }
    }

    qDebug().noquote() << QObject::tr("collecting statics");
    this->collecting_statics = true;
    for(int player_id = 0;player_id < this->player_number;player_id ++) {
        this->round_deal = vector<int>{-1,-1,-1,-1};
        //#pragma omp parallel
        {
            //#pragma omp single
            {
                //this->distributing_task = true;
                cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id], this->iteration_number, this->initial_board_long,0);
            }
        }
    }
    this->collecting_statics = false;
    this->statics_collected = true;
    qDebug().noquote() << QObject::tr("statics collected");

    uint64_t total_time_ms = timeSinceEpochMillisec() - begintime;
    qDebug().noquote() << "-------------------";
    qDebug().noquote() << QObject::tr("Total training time: ") << float(total_time_ms) / 1000 << QObject::tr(" seconds.");

    if(!this->logfile.empty()) {
        fileWriter.flush();
        fileWriter.close();
    }

}

void PCfrSolver::exchangeRange(json& strategy,int rank1,int rank2,const shared_ptr<ActionNode>& one_node){
    if(rank1 == rank2)return;
    int player = one_node->getPlayer();
    vector<string> range_strs;
    vector<vector<float>> strategies;

    for(std::size_t i = 0;i < this->ranges[player].size();i ++){
        string one_range_str = this->ranges[player][i].toString();
        if(!strategy.contains(one_range_str)){
            qWarning() << "Warning: Hand" << QString::fromStdString(one_range_str) << "not found in strategy for exchangeRange.";
            vector<float> default_strategy(one_node->getActions().size(), 0.0f);
            if (!default_strategy.empty()) default_strategy[0] = 1.0f;
            range_strs.push_back(one_range_str);
            strategies.push_back(default_strategy);
            continue;
        }
        vector<float> one_strategy = strategy[one_range_str];
        range_strs.push_back(one_range_str);
        strategies.push_back(one_strategy);
    }
    exchange_color(strategies,this->ranges[player],rank1,rank2);

    for(std::size_t i = 0;i < this->ranges[player].size();i ++) {
        string one_range_str = this->ranges[player][i].toString();
        vector<float> one_strategy = strategies[i];
        strategy[one_range_str] = one_strategy;
    }
}

void PCfrSolver::reConvertJson(const shared_ptr<GameTreeNode>& node,json& strategy,string key,int depth,int max_depth,vector<string> prefix,int deal,vector<vector<int>> exchange_color_list) {
    if(depth >= max_depth) return;
    if(node->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        json* retval;
        if(key != ""){
            strategy[key] = json();
            retval = &(strategy[key]);
        }else{
            retval = &strategy;
        }

        shared_ptr<ActionNode> one_node = std::dynamic_pointer_cast<ActionNode>(node);

        vector<string> actions_str;
        for(GameActions one_action:one_node->getActions()) actions_str.push_back(one_action.toString());

        (*retval)["actions"] = actions_str;
        (*retval)["player"] = one_node->getPlayer();

        (*retval)["childrens"] = json();

        for(std::size_t i = 0;i < one_node->getActions().size();i ++){
            GameActions& one_action = one_node->getActions()[i];
            shared_ptr<GameTreeNode> one_child = one_node->getChildrens()[i];
            vector<string> new_prefix(prefix);
            new_prefix.push_back(one_action.toString());
            this->reConvertJson(one_child,(*retval)["childrens"],one_action.toString(),depth,max_depth,new_prefix,deal,exchange_color_list);
        }
        if((*retval)["childrens"].empty()){
            (*retval).erase("childrens");
        }
        shared_ptr<Trainable> trainable = one_node->getTrainable(deal,false);
        if(trainable != nullptr) {
            (*retval)["strategy"] = trainable->dump_strategy(false);
            for(vector<int> one_exchange:exchange_color_list){
                int rank1 = one_exchange[0];
                int rank2 = one_exchange[1];
                this->exchangeRange((*retval)["strategy"]["strategy"],rank1,rank2,one_node);

            }
        }
        (*retval)["node_type"] = "action_node";

    }else if(node->getType() == GameTreeNode::GameTreeNodeType::SHOWDOWN) {
    }else if(node->getType() == GameTreeNode::GameTreeNodeType::TERMINAL) {
    }else if(node->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        json* retval;
        if(key != ""){
            strategy[key] = json();
            retval = &(strategy[key]);
        }else{
            retval = &strategy;
        }

        shared_ptr<ChanceNode> chanceNode = std::dynamic_pointer_cast<ChanceNode>(node);
        const vector<Card>& cards = chanceNode->getCards();
        shared_ptr<GameTreeNode> childerns = chanceNode->getChildren();
        vector<string> card_strs;
        for(Card card:cards)
            card_strs.push_back(card.toString());

        (*retval)["dealcards"] = json();
        for(std::size_t i = 0;i < cards.size();i ++){
            vector<vector<int>> new_exchange_color_list(exchange_color_list);
            Card& one_card = const_cast<Card &>(cards[i]);
            vector<string> new_prefix(prefix);
            new_prefix.push_back("Chance:" + one_card.toString());

            std::size_t card = i;

            int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
            if(offset < 0) {
                for(std::size_t x = 0;x < cards.size();x ++){
                    if(
                            Card::card2int(cards[x]) ==
                            (Card::card2int(cards[card]) + offset)
                      ){
                        card = x;
                        break;
                    }
                }
                if(card == i){
                    throw runtime_error("isomorphism not found while dump strategy");
                }
                vector<int> one_exchange{one_card.getCardInt() % 4,one_card.getCardInt() % 4 + offset};
                new_exchange_color_list.push_back(one_exchange);
            }

            int card_num = this->deck.getCards().size();
            int new_deal;
            if(deal == 0){
                new_deal = card + 1;
            } else if (deal > 0 && deal <= card_num){
                int origin_deal = deal - 1;

#ifdef DEBUG
                if(origin_deal == card) throw runtime_error("deal should not be equal");
#endif
                new_deal = card_num * origin_deal + card;
                new_deal += (1 + card_num);
            } else{
                throw runtime_error(tfm::format("deal out of range : %s ",deal));
            }

            if(exchange_color_list.size() > 1){
                throw runtime_error("exchange color list shouldn't be exceed size 1 here");
            }

            string one_card_str = one_card.toString();
            if(exchange_color_list.size() == 1) {
                int rank1 = exchange_color_list[0][0];
                int rank2 = exchange_color_list[0][1];
                if(one_card.getCardInt() % 4 == rank1){
                    one_card_str = Card::intCard2Str(one_card.getCardInt() - rank1 + rank2);
                }else if(one_card.getCardInt() % 4 == rank2){
                    one_card_str = Card::intCard2Str(one_card.getCardInt() - rank2 + rank1);
                }

            }

            this->reConvertJson(childerns,(*retval)["dealcards"],one_card_str,depth + 1,max_depth,new_prefix,new_deal,new_exchange_color_list);
        }
        if((*retval)["dealcards"].empty()){
            (*retval)["deal_number"] = 0;
            (*retval).erase("dealcards");
        } else {
            (*retval)["deal_number"] = (*retval)["dealcards"].size();
        }
        (*retval)["node_type"] = "chance_node";
    }else{
        throw runtime_error("node type unknown!!");
    }
}

static json dump_node_strategy(const shared_ptr<GameTreeNode>& node) {
    if (!node) return json();
    json ret;
    if (node->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<ActionNode>(node);
        ret["node_type"] = "action_node";
        ret["player"] = action_node->getPlayer();
        
        json children_json = json::array();
        const auto& children = action_node->getChildrens();
        for (size_t i = 0; i < children.size(); ++i) {
            children_json.push_back(dump_node_strategy(children[i]));
        }
        ret["children"] = children_json;

        json trainable_json = json::array();
        const auto& trainables = action_node->getTrainables();
        for (size_t i = 0; i < trainables.size(); ++i) {
            if (trainables[i]) {
                json t_dump = trainables[i]->dump_strategy(false);
                t_dump["evs"] = trainables[i]->dump_evs()["evs"];
                trainable_json.push_back(t_dump);
            } else {
                trainable_json.push_back(json()); // null
            }
        }
        ret["trainables"] = trainable_json;
        
    } else if (node->getType() == GameTreeNode::GameTreeNodeType::SHOWDOWN) {
        ret["node_type"] = "showdown_node";
    } else if (node->getType() == GameTreeNode::GameTreeNodeType::TERMINAL) {
        ret["node_type"] = "terminal_node";
    } else if (node->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        auto chance_node = dynamic_pointer_cast<ChanceNode>(node);
        ret["node_type"] = "chance_node";
        ret["child"] = dump_node_strategy(chance_node->getChildren());
    }
    return ret;
}

json PCfrSolver::dumps_strategy() {
    return dump_node_strategy(this->tree->getRoot());
}

void PCfrSolver::loads_strategy_and_evs(const shared_ptr<GameTreeNode>& node, const json& data) {
    if (!node || data.is_null()) return;

    if (node->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<ActionNode>(node);
        
        if (data.contains("trainables") && data["trainables"].is_array()) {
            json trainables_data = data["trainables"];
            int action_num = action_node->getActions().size();
            const auto& ph = this->ranges[action_node->getPlayer()];
            
            for (size_t i = 0; i < trainables_data.size(); ++i) {
                if (trainables_data[i].is_null()) continue;
                json t_data = trainables_data[i];
                if (t_data.contains("strategy") && t_data.contains("evs")) {
                    json strategy_json = t_data["strategy"];
                    json evs_json = t_data["evs"];
                    
                    vector<float> loaded_strat(action_num * ph.size(), 0.0f);
                    vector<float> loaded_evs(action_num * ph.size(), 0.0f);
                    
                    for (size_t h = 0; h < ph.size(); ++h) {
                        string hand_str = ph[h].toString();
                        if (strategy_json.contains(hand_str)) {
                            vector<float> strat_vals = strategy_json[hand_str].get<vector<float>>();
                            for (int j = 0; j < action_num; ++j) {
                                if (j < (int)strat_vals.size()) loaded_strat[j * ph.size() + h] = strat_vals[j];
                            }
                        }
                        if (evs_json.contains(hand_str)) {
                            vector<float> ev_vals = evs_json[hand_str].get<vector<float>>();
                            for (int j = 0; j < action_num; ++j) {
                                if (j < (int)ev_vals.size()) loaded_evs[j * ph.size() + h] = ev_vals[j];
                            }
                        }
                    }
                    
                    // Force instantiate the trainable if not present
                    shared_ptr<Trainable> t = action_node->getTrainable(i, true, this->use_halffloats);
                    if (t) {
                        t->load_strategy_and_evs(loaded_strat, loaded_evs);
                    }
                }
            }
        }
        
        const auto& children = action_node->getChildrens();
        if (data.contains("children") && data["children"].is_array()) {
            json children_json = data["children"];
            for (size_t i = 0; i < std::min(children.size(), children_json.size()); ++i) {
                loads_strategy_and_evs(children[i], children_json[i]);
            }
        }
    } else if (node->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        auto chance_node = dynamic_pointer_cast<ChanceNode>(node);
        if (data.contains("child")) {
            loads_strategy_and_evs(chance_node->getChildren(), data["child"]);
        }
    }
}

vector<vector<vector<float>>> PCfrSolver::get_strategy(const shared_ptr<ActionNode>& node,vector<Card> chance_cards){
    int expected = GameTreeNode::gameRound2int(node->getRound()) - GameTreeNode::gameRound2int(this->root_round);
    if (expected < 0) expected = 0;
    if ((int)chance_cards.size() > expected) {
        chance_cards.erase(chance_cards.begin(), chance_cards.end() - expected);
    }

    int deal = 0;
    int card_num = this->deck.getCards().size();
    vector<vector<int>> exchange_color_list;

    vector<vector<vector<float>>> ret_strategy = vector<vector<vector<float>>>(52);
    for(int i = 0;i < 52;i ++){
        ret_strategy[i] = vector<vector<float>>(52);
        for(int j = 0;j < 52;j ++){
            ret_strategy[i][j] = vector<float>();
        }
    }

    vector<Card>& cards = this->deck.getCards();

    for(Card one_card: chance_cards){
        int card = one_card.getNumberInDeckInt();
        int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
        if(offset < 0) {
            for(std::size_t x = 0;x < cards.size();x ++){
                if(
                        Card::card2int(cards[x]) ==
                        (Card::card2int(cards[card]) + offset)
                  ){
                    card = x;
                    break;
                }
            }
            if(card == one_card.getNumberInDeckInt()){
                throw runtime_error("isomorphism not found while dump strategy");
            }
            vector<int> one_exchange{one_card.getCardInt() % 4,one_card.getCardInt() % 4 + offset};
            exchange_color_list.push_back(one_exchange);
        }

        int new_deal;
        if(deal == 0){
            new_deal = card + 1;
        } else if (deal > 0 && deal <= card_num){
            int origin_deal = deal - 1;
            new_deal = card_num * origin_deal + card;
            new_deal += (1 + card_num);
        } else{
            throw runtime_error(tfm::format("deal out of range : %s ",deal));
        }
        deal = new_deal;
    }
    shared_ptr<Trainable> trainable = node->getTrainable(deal,true,this->use_halffloats);
    if (!trainable) {
        return ret_strategy;
    }
    json retjson = trainable->dump_strategy(false);

    for(vector<int> one_exchange:exchange_color_list){
        int rank1 = one_exchange[0];
        int rank2 = one_exchange[1];
        this->exchangeRange((retjson["strategy"]),rank1,rank2,node);
    }

    int player = node->getPlayer();

    json& strategy = retjson["strategy"];
    for(std::size_t i = 0;i < this->ranges[player].size();i ++){
        PrivateCards pc = this->ranges[player][i];
        string one_range_str = pc.toString();
        if(!strategy.contains(one_range_str)){
            qWarning() << "Warning: Hand" << QString::fromStdString(one_range_str) << "not found in strategy. Using default strategy.";
            continue;
        }
        vector<float> one_strategy = strategy[one_range_str];
        bool intercept = false;
        for(auto one_card:chance_cards){
            if(one_card.getCardInt() == pc.card1 || one_card.getCardInt() == pc.card2){
                intercept = true;
            }
        }
        if(intercept) continue;
        ret_strategy[pc.card1][pc.card2] = one_strategy;
    }
    return ret_strategy;
}

vector<vector<vector<float>>> PCfrSolver::get_evs(const shared_ptr<ActionNode>& node,vector<Card> chance_cards){
    int expected = GameTreeNode::gameRound2int(node->getRound()) - GameTreeNode::gameRound2int(this->root_round);
    if (expected < 0) expected = 0;
    if ((int)chance_cards.size() > expected) {
        chance_cards.erase(chance_cards.begin(), chance_cards.end() - expected);
    }

    // If solving process has not finished, then no evs is set, therefore we shouldn't return anything
    int deal = 0;
    int card_num = this->deck.getCards().size();
    vector<vector<int>> exchange_color_list;

    vector<vector<vector<float>>> ret_evs = vector<vector<vector<float>>>(52);
    for(int i = 0;i < 52;i ++){
        ret_evs[i] = vector<vector<float>>(52);
        for(int j = 0;j < 52;j ++){
            ret_evs[i][j] = vector<float>();
        }
    }

    vector<Card>& cards = this->deck.getCards();

    for(Card one_card: chance_cards){
        int card = one_card.getNumberInDeckInt();
        int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
        if(offset < 0) {
            for(std::size_t x = 0;x < cards.size();x ++){
                if(
                        Card::card2int(cards[x]) ==
                        (Card::card2int(cards[card]) + offset)
                  ){
                    card = x;
                    break;
                }
            }
            if(card == one_card.getNumberInDeckInt()){
                throw runtime_error("isomorphism not found while dump evs");
            }
            vector<int> one_exchange{one_card.getCardInt() % 4,one_card.getCardInt() % 4 + offset};
            exchange_color_list.push_back(one_exchange);
        }

        int new_deal;
        if(deal == 0){
            new_deal = card + 1;
        } else if (deal > 0 && deal <= card_num){
            int origin_deal = deal - 1;
            new_deal = card_num * origin_deal + card;
            new_deal += (1 + card_num);
        } else{
            throw runtime_error(tfm::format("deal out of range : %s ",deal));
        }
        deal = new_deal;
    }
    shared_ptr<Trainable> trainable = node->getTrainable(deal,true,this->use_halffloats);
    if (!trainable) {
        return ret_evs;
    }
    json retjson = trainable->dump_evs();

    for(vector<int> one_exchange:exchange_color_list){
        int rank1 = one_exchange[0];
        int rank2 = one_exchange[1];
        this->exchangeRange((retjson["evs"]),rank1,rank2,node);
    }

    int player = node->getPlayer();

    json& evs = retjson["evs"];
    for(std::size_t i = 0;i < this->ranges[player].size();i ++){
        PrivateCards pc = this->ranges[player][i];
        string one_range_str = pc.toString();
        if(!evs.contains(one_range_str)){
            qWarning() << "Warning: Hand" << QString::fromStdString(one_range_str) << "not found in evs. Using default EV.";
            continue;
        }
        vector<float> one_evs = evs[one_range_str];
        bool intercept = false;
        for(auto one_card:chance_cards){
            if(one_card.getCardInt() == pc.card1 || one_card.getCardInt() == pc.card2){
                intercept = true;
            }
        }
        if(intercept) continue;
        ret_evs[pc.card1][pc.card2] = one_evs;
    }
    return ret_evs;
}

json PCfrSolver::dumps(bool with_status,int depth) {
    if(with_status == true){
        throw runtime_error("");
    }
    json retjson;
    this->reConvertJson(this->tree->getRoot(),retjson,"",0,depth,vector<string>({"begin"}),0,vector<vector<int>>());
    return std::move(retjson);
}


shared_ptr<Trainable> PCfrSolver::get_trainable(const shared_ptr<ActionNode>& node,vector<Card> chance_cards) {
    int expected = GameTreeNode::gameRound2int(node->getRound()) - GameTreeNode::gameRound2int(this->root_round);
    if (expected < 0) expected = 0;
    if ((int)chance_cards.size() > expected) {
        chance_cards.erase(chance_cards.begin(), chance_cards.end() - expected);
    }

    int deal = 0;
    int card_num = this->deck.getCards().size();
    vector<Card>& cards = this->deck.getCards();

    for(Card one_card: chance_cards){
        int card = one_card.getNumberInDeckInt();
        int offset = this->color_iso_offset[deal][one_card.getCardInt() % 4];
        if(offset < 0) {
            for(std::size_t x = 0;x < cards.size();x ++){
                if(
                        Card::card2int(cards[x]) ==
                        (Card::card2int(cards[card]) + offset)
                  ){
                    card = x;
                    break;
                }
            }
            if(card == one_card.getNumberInDeckInt()){
                throw runtime_error("isomorphism not found");
            }
        }

        int new_deal;
        if(deal == 0){
            new_deal = card + 1;
        } else if (deal > 0 && deal <= card_num){
            int origin_deal = deal - 1;
            new_deal = card_num * origin_deal + card;
            new_deal += (1 + card_num);
        } else{
            throw runtime_error(tfm::format("deal out of range : %s ",deal));
        }
        deal = new_deal;
    }
    return node->getTrainable(deal, true, this->use_halffloats);
}

#include <zlib.h>
#include <stdexcept>
#include <cstring>

static void save_node_binary(gzFile file, const shared_ptr<GameTreeNode>& node) {
    if (!node) return;
    if (node->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<ActionNode>(node);
        const auto& trainables = action_node->getTrainables();
        uint32_t num_trainables = trainables.size();
        gzwrite(file, &num_trainables, sizeof(num_trainables));
        for (size_t i = 0; i < num_trainables; ++i) {
            if (!trainables[i]) {
                uint8_t present = 0;
                gzwrite(file, &present, sizeof(present));
            } else {
                uint8_t present = 1;
                gzwrite(file, &present, sizeof(present));
                
                // Write strategy
                const vector<float>& strategy = trainables[i]->getAverageStrategy();
                uint32_t strat_size = strategy.size();
                gzwrite(file, &strat_size, sizeof(strat_size));
                if (strat_size > 0) {
                    gzwrite(file, strategy.data(), strat_size * sizeof(float));
                }
                
                // Write EVs
                vector<float> evs = trainables[i]->getEvs();
                uint32_t evs_size = evs.size();
                gzwrite(file, &evs_size, sizeof(evs_size));
                if (evs_size > 0) {
                    gzwrite(file, evs.data(), evs_size * sizeof(float));
                }
            }
        }
        for (const auto& child : action_node->getChildrens()) {
            save_node_binary(file, child);
        }
    } else if (node->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        auto chance_node = dynamic_pointer_cast<ChanceNode>(node);
        save_node_binary(file, chance_node->getChildren());
    }
}

static void load_node_binary(gzFile file, const shared_ptr<GameTreeNode>& node, int use_halffloats) {
    if (!node) return;
    if (node->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<ActionNode>(node);
        uint32_t num_trainables = 0;
        if (gzread(file, &num_trainables, sizeof(num_trainables)) != sizeof(num_trainables)) {
            throw runtime_error("Format error: failed to read number of trainables");
        }
        for (size_t i = 0; i < num_trainables; ++i) {
            uint8_t present = 0;
            if (gzread(file, &present, sizeof(present)) != sizeof(present)) {
                throw runtime_error("Format error: failed to read trainable presence flag");
            }
            if (present) {
                uint32_t strat_size = 0;
                if (gzread(file, &strat_size, sizeof(strat_size)) != sizeof(strat_size)) {
                    throw runtime_error("Format error: failed to read strategy size");
                }
                vector<float> strat(strat_size);
                if (strat_size > 0) {
                    if (gzread(file, strat.data(), strat_size * sizeof(float)) != (int)(strat_size * sizeof(float))) {
                        throw runtime_error("Format error: failed to read strategy data");
                    }
                }
                uint32_t evs_size = 0;
                if (gzread(file, &evs_size, sizeof(evs_size)) != sizeof(evs_size)) {
                    throw runtime_error("Format error: failed to read EV size");
                }
                vector<float> evs(evs_size);
                if (evs_size > 0) {
                    if (gzread(file, evs.data(), evs_size * sizeof(float)) != (int)(evs_size * sizeof(float))) {
                        throw runtime_error("Format error: failed to read EV data");
                    }
                }
                
                shared_ptr<Trainable> t = action_node->getTrainable(i, true, use_halffloats);
                if (t) {
                    t->load_strategy_and_evs(strat, evs);
                }
            }
        }
        for (const auto& child : action_node->getChildrens()) {
            load_node_binary(file, child, use_halffloats);
        }
    } else if (node->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        auto chance_node = dynamic_pointer_cast<ChanceNode>(node);
        load_node_binary(file, chance_node->getChildren(), use_halffloats);
    }
}

bool PCfrSolver::save_solve_to_file(const string& filepath, const string& config_str, const string& metadata_json_str) {
    gzFile file = gzopen(filepath.c_str(), "wb");
    if (!file) return false;
    
    char magic[4] = {'T', 'X', 'S', 'B'};
    gzwrite(file, magic, 4);
    
    uint32_t version = 1;
    gzwrite(file, &version, 4);
    
    uint32_t config_len = config_str.size();
    gzwrite(file, &config_len, 4);
    if (config_len > 0) {
        gzwrite(file, config_str.data(), config_len);
    }
    
    uint32_t metadata_len = metadata_json_str.size();
    gzwrite(file, &metadata_len, 4);
    if (metadata_len > 0) {
        gzwrite(file, metadata_json_str.data(), metadata_len);
    }
    
    try {
        save_node_binary(file, this->tree->getRoot());
    } catch (...) {
        gzclose(file);
        return false;
    }
    
    gzclose(file);
    return true;
}

bool PCfrSolver::load_solve_from_file(const string& filepath) {
    gzFile file = gzopen(filepath.c_str(), "rb");
    if (!file) return false;
    
    char magic[4];
    if (gzread(file, magic, 4) != 4 || magic[0] != 'T' || magic[1] != 'X' || magic[2] != 'S' || magic[3] != 'B') {
        gzclose(file);
        return false;
    }
    
    uint32_t version = 0;
    gzread(file, &version, 4);
    
    uint32_t config_len = 0;
    gzread(file, &config_len, 4);
    if (config_len > 0) {
        vector<char> temp(config_len);
        gzread(file, temp.data(), config_len);
    }
    
    uint32_t metadata_len = 0;
    gzread(file, &metadata_len, 4);
    if (metadata_len > 0) {
        vector<char> temp(metadata_len);
        gzread(file, temp.data(), metadata_len);
    }
    
    try {
        load_node_binary(file, this->tree->getRoot(), this->use_halffloats);
    } catch (...) {
        gzclose(file);
        return false;
    }
    
    gzclose(file);
    return true;
}

void PCfrSolver::dump_training_sample(int row_id, float exploitability, float solve_duration_sec) {
    // Get the root as ActionNode to extract EVs
    shared_ptr<GameTreeNode> root = tree->getRoot();
    if (root->getType() != GameTreeNode::ACTION) {
        cout << "Error: root node is not an action node, cannot dump EVs." << endl;
        return;
    }
    shared_ptr<ActionNode> action_root = std::dynamic_pointer_cast<ActionNode>(root);

    // Get per-hand EVs from the converged strategy (public API)
    vector<vector<vector<float>>> evs = this->get_evs(action_root, {});

    const vector<PrivateCards>& oop_hands = pcm.getPreflopCards(0);
    const vector<PrivateCards>& ip_hands = pcm.getPreflopCards(1);

    // Flatten into 1326-dim vectors (one entry per unique unordered card pair)
    vector<float> oop_range_flat(1326, 0.0f);
    vector<float> ip_range_flat(1326, 0.0f);
    vector<float> oop_evs_flat(1326, 0.0f);
    vector<float> ip_evs_flat(1326, 0.0f);

    auto card_pair_to_flat_idx = [](int c1, int c2) -> int {
        int lo = min(c1, c2);
        int hi = max(c1, c2);
        int idx = 0;
        for (int a = 0; a < lo; a++) idx += (51 - a);
        idx += (hi - lo - 1);
        return idx;
    };

    // Fill OOP EVs
    for (size_t i = 0; i < oop_hands.size(); i++) {
        int c1 = oop_hands[i].card1;
        int c2 = oop_hands[i].card2;
        int flat_idx = card_pair_to_flat_idx(c1, c2);
        
        oop_range_flat[flat_idx] = oop_hands[i].weight;

        // EVs from get_evs are indexed [card1_int][card2_int][player]
        int ci1 = oop_hands[i].card1;
        int ci2 = oop_hands[i].card2;
        if (!evs[ci1][ci2].empty()) {
            oop_evs_flat[flat_idx] = evs[ci1][ci2][0]; // player 0 = OOP
        }
    }

    // Fill IP EVs
    for (size_t i = 0; i < ip_hands.size(); i++) {
        int c1 = ip_hands[i].card1;
        int c2 = ip_hands[i].card2;
        int flat_idx = card_pair_to_flat_idx(c1, c2);
        
        ip_range_flat[flat_idx] = ip_hands[i].weight;

        int ci1 = ip_hands[i].card1;
        int ci2 = ip_hands[i].card2;
        if (!evs[ci1][ci2].empty() && evs[ci1][ci2].size() > 1) {
            ip_evs_flat[flat_idx] = evs[ci1][ci2][1]; // player 1 = IP
        }
    }

    DBManager::instance().updateTrainingSample(row_id, oop_range_flat, ip_range_flat, oop_evs_flat, ip_evs_flat, exploitability, solve_duration_sec);
    cout << "Training sample saved to database." << endl;
}
