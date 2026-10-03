//
// Created by Xuefeng Huang on 2020/1/31.
//

#ifndef TEXASSOLVER_CFRPLUSTRAINABLE_H
#define TEXASSOLVER_CFRPLUSTRAINABLE_H

#include <include/nodes/ActionNode.h>
#include <include/ranges/PrivateCards.h>
#include "include/trainable/Trainable.h"
using namespace std;

class CfrPlusTrainable : public Trainable{
private:
    shared_ptr<ActionNode> action_node;
    vector<PrivateCards> privateCards;
    int action_number;
    int card_number;
    vector<float> r_plus;
    vector<float> r_plus_sum;
    vector<float> cum_r_plus;
    vector<float> cum_r_plus_sum;
    vector<float> regrets;
    vector<float> retval;
    bool locked_ = false;
    vector<float> locked_strategy_;
    vector<bool> locked_mask_;
public:
    CfrPlusTrainable();
    CfrPlusTrainable(shared_ptr<ActionNode> action_node, vector<PrivateCards> privateCards);
    bool isAllZeros(vector<float> input_array);

    const vector<float>& getAverageStrategy() override;

    const vector<float>& getcurrentStrategy() override;

    void setCumRegrets(const vector<float>& regrets) override;

    void updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) override;

    json dump_strategy(bool with_state) override;

    json dump_evs() override;

    void setEv(const vector<float>& evs) override;

    void load_strategy_and_evs(const vector<float>& strat, const vector<float>& evs) override;

    void copyStrategy(shared_ptr<Trainable> other_trainable) override;

    TrainableType get_type() override;

    void lockStrategy(const vector<float>& locked_strategy, const vector<bool>& locked_mask) override;
    void unlockStrategy() override;
    bool isLocked() const override;
    const vector<bool>& getLockedMask() const override;
};


#endif //TEXASSOLVER_CFRPLUSTRAINABLE_H
