//
// Created by Xuefeng Huang on 2020/1/31.
//

#ifndef TEXASSOLVER_TRAINABLE_H
#define TEXASSOLVER_TRAINABLE_H
#include <vector>
#include "include/json.hpp"
using namespace std;
using json = nlohmann::json;

class Trainable {
public:
    enum TrainableType {
        CFR_PLUS_TRAINABLE,
        DISCOUNTED_CFR_TRAINABLE
    };
    virtual const vector<float>& getAverageStrategy() = 0;
    virtual const vector<float>& getcurrentStrategy() = 0;
    virtual void updateRegrets(const vector<float>& regrets,int iteration_number,const vector<float>& reach_probs) = 0;
    virtual void setEv(const vector<float>& evs) = 0;
    virtual void copyStrategy(shared_ptr<Trainable> other_trainable) = 0;
    virtual void setCumRegrets(const vector<float>& regrets) {}
    virtual json dump_strategy(bool with_state) = 0;
    virtual json dump_evs() = 0;
    virtual vector<float> getEvs() { return vector<float>(); }
    virtual void load_strategy_and_evs(const vector<float>& strat, const vector<float>& evs) = 0;
    virtual TrainableType get_type() = 0;

    // Node locking: freeze strategy for specific hands
    virtual void lockStrategy(const vector<float>& locked_strategy, const vector<bool>& locked_mask) = 0;
    virtual void unlockStrategy() = 0;
    virtual bool isLocked() const = 0;
    virtual const vector<bool>& getLockedMask() const = 0;
};


#endif //TEXASSOLVER_TRAINABLE_H
