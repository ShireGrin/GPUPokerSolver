//
// Created by Xuefeng Huang on 2020/1/31.
//

#include "include/trainable/CfrPlusTrainable.h"

CfrPlusTrainable::CfrPlusTrainable() {

}

CfrPlusTrainable::CfrPlusTrainable(shared_ptr<ActionNode> action_node, vector<PrivateCards> privateCards) {
    this->action_node = action_node;
    this->privateCards = privateCards;
    this->action_number = action_node->getChildrens().size();
    this->card_number = privateCards.size();

    this->r_plus = vector<float>(this->action_number * this->card_number);
    this->r_plus_sum = vector<float>(this->card_number);

    this->cum_r_plus = vector<float>(this->action_number * this->card_number);
    this->cum_r_plus_sum = vector<float>(this->card_number);
    this->retval = vector<float>(this->action_number * this->card_number);
}

bool CfrPlusTrainable::isAllZeros(vector<float> input_array) {
    for(float i:input_array){
        if (i != 0)return false;
    }
    return true;
}

const vector<float>& CfrPlusTrainable::getAverageStrategy() {
    if(locked_ && locked_mask_.empty()) return locked_strategy_;
    
    // Compute normalized average strategy from cum_r_plus
    for (int private_id = 0; private_id < this->card_number; private_id++) {
        float r_plus_sum = 0;
        for (int action_id = 0; action_id < action_number; action_id++) {
            int index = action_id * this->card_number + private_id;
            r_plus_sum += this->cum_r_plus[index];
        }

        for (int action_id = 0; action_id < action_number; action_id++) {
            int index = action_id * this->card_number + private_id;
            if(r_plus_sum > 0) {
                this->retval[index] = this->cum_r_plus[index] / r_plus_sum;
            }else{
                this->retval[index] = 1.0f / this->action_number;
            }
        }
    }
    
    // Override locked hands
    if (locked_) {
        for (int private_id = 0; private_id < this->card_number; private_id++) {
            if (locked_mask_.empty() || locked_mask_[private_id]) {
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * this->card_number + private_id;
                    this->retval[index] = locked_strategy_[index];
                }
            }
        }
    }
    
    return this->retval;
}

void CfrPlusTrainable::setCumRegrets(const vector<float>& regrets) {
    this->cum_r_plus.assign(regrets.begin(), regrets.end());
}

const vector<float>& CfrPlusTrainable::getcurrentStrategy() {
    if(locked_) {
        // First compute normal strategy into retval
        if(this->r_plus_sum.empty()){
            fill(retval.begin(),retval.end(),1.0 / this->action_number);
        }else {
            for (int action_id = 0; action_id < action_number; action_id++) {
                for (int private_id = 0; private_id < this->card_number; private_id++) {
                    int index = action_id * this->card_number + private_id;
                    if(this->r_plus_sum[private_id] != 0) {
                        retval[index] = this->r_plus[index] / this->r_plus_sum[private_id];
                    }else{
                        retval[index] = 1.0 / this->action_number;
                    }
                    if(retval[index] != retval[index]) throw runtime_error("nan found");
                }
            }
        }
        // Now override locked hands
        for (int private_id = 0; private_id < this->card_number; private_id++) {
            if (locked_mask_.empty() || locked_mask_[private_id]) {
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * this->card_number + private_id;
                    retval[index] = locked_strategy_[index];
                }
            }
        }
        return retval;
    }
    if(this->r_plus_sum.empty()){
        fill(retval.begin(),retval.end(),1.0 / this->action_number);
    }else {
        for (int action_id = 0; action_id < action_number; action_id++) {
            for (int private_id = 0; private_id < this->card_number; private_id++) {
                int index = action_id * this->card_number + private_id;
                if(this->r_plus_sum[private_id] != 0) {
                    retval[index] = this->r_plus[index] / this->r_plus_sum[private_id];
                }else{
                    retval[index] = 1.0 / this->action_number;
                }
                if(retval[index] != retval[index]) throw runtime_error("nan found");
            }
        }
    }
    return retval;
}

void CfrPlusTrainable::updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) {
    if(locked_ && locked_mask_.empty()) return;
    this->regrets = regrets;
    if(regrets.size() != this->action_number * this->card_number) throw runtime_error("length not match");

    //Arrays.fill(this.r_plus_sum,0);
    fill(r_plus_sum.begin(),r_plus_sum.end(),0);
    fill(cum_r_plus_sum.begin(),cum_r_plus_sum.end(),0);
    for (int action_id = 0;action_id < action_number;action_id ++) {
        for(int private_id = 0;private_id < this->card_number;private_id ++){
            if (locked_ && !locked_mask_.empty() && locked_mask_[private_id]) continue;
            int index = action_id * this->card_number + private_id;
            float one_reg = regrets[index];

            // 更新 R+
            this->r_plus[index] = max((float)0.0,one_reg + this->r_plus[index]);
            this->r_plus_sum[private_id] += this->r_plus[index];

            // 更新累计策略
            this->cum_r_plus[index] += this->r_plus[index] * iteration_number;
            this->cum_r_plus_sum[private_id] += this->cum_r_plus[index];
        }
    }
}

json CfrPlusTrainable::dump_strategy(bool with_state) {
    if(with_state) throw runtime_error("state storage not implemented");

    json strategy;
    vector<float> average_strategy = this->getcurrentStrategy();
    vector<GameActions> game_actions = action_node->getActions();
    vector<string> actions_str;
    for(GameActions one_action:game_actions) actions_str.push_back(
                one_action.toString()
        );

    //SolverEnvironment se = SolverEnvironment.getInstance();
    //Compairer comp = se.getCompairer();

    for(int i = 0;i < this->privateCards.size();i ++){
        PrivateCards one_private_card = this->privateCards[i];
        vector<float> one_strategy(this->action_number);

        /*
        int[] initialBoard = new int[]{
                Card.strCard2int("Kd"),
                Card.strCard2int("Jd"),
                Card.strCard2int("Td"),
                Card.strCard2int("7s"),
                Card.strCard2int("8s")
        };
        int rank = comp.get_rank(new int[]{one_private_card.card1,one_private_card.card2},initialBoard);
         */

        for(int j = 0;j < this->action_number;j ++){
            int strategy_index = j * this->privateCards.size() + i;
            one_strategy[j] = average_strategy[strategy_index];
        }
        strategy[tfm::format("%s",one_private_card.toString())] = one_strategy;
    }

    json retjson;
    retjson["actions"] = actions_str;
    retjson["strategy"] = strategy;
    return retjson;
}

Trainable::TrainableType CfrPlusTrainable::get_type() {
    return Trainable::CFR_PLUS_TRAINABLE;
}

void CfrPlusTrainable::load_strategy_and_evs(const vector<float>& strat, const vector<float>& evs_vec) {
    int hand_num = this->card_number;
    for (int i = 0; i < action_number * hand_num; ++i) {
        if (i < (int)strat.size()) {
            this->cum_r_plus[i] = strat[i];
        }
    }
}

json CfrPlusTrainable::dump_evs() {
    // CfrPlusTrainable does not track EVs, return empty
    json retjson;
    retjson["actions"] = vector<string>();
    retjson["evs"] = json::object();
    return retjson;
}

void CfrPlusTrainable::setEv(const vector<float>& evs) {
    // CfrPlusTrainable does not track EVs
}

void CfrPlusTrainable::copyStrategy(shared_ptr<Trainable> other_trainable) {
    shared_ptr<CfrPlusTrainable> trainable = dynamic_pointer_cast<CfrPlusTrainable>(other_trainable);
    this->r_plus.assign(trainable->r_plus.begin(), trainable->r_plus.end());
    this->cum_r_plus.assign(trainable->cum_r_plus.begin(), trainable->cum_r_plus.end());
}

void CfrPlusTrainable::lockStrategy(const vector<float>& locked_strategy, const vector<bool>& locked_mask) {
    if(locked_strategy.size() != this->action_number * this->card_number) {
        throw runtime_error(tfm::format(
            "locked_strategy size %s does not match expected %s",
            locked_strategy.size(), this->action_number * this->card_number));
    }
    locked_strategy_ = locked_strategy;
    locked_mask_ = locked_mask;
    locked_ = true;
}

void CfrPlusTrainable::unlockStrategy() {
    locked_ = false;
    locked_strategy_.clear();
    locked_mask_.clear();
}

bool CfrPlusTrainable::isLocked() const {
    return locked_;
}

const vector<bool>& CfrPlusTrainable::getLockedMask() const {
    return locked_mask_;
}
