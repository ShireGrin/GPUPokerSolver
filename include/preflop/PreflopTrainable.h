#ifndef TEXASSOLVER_PREFLOPTRAINABLE_H
#define TEXASSOLVER_PREFLOPTRAINABLE_H

#include "include/trainable/Trainable.h"
#include "include/ranges/PrivateCards.h"
#include <vector>

using namespace std;

class PreflopTrainable : public Trainable {
private:
    int action_number;
    int hand_number;
    vector<PrivateCards>* private_cards;

    vector<float> r_plus;          // Cumulative positive regrets
    vector<float> r_plus_sum;      // Sum of positive regrets per hand
    vector<float> cum_r_plus;      // Cumulative strategy weights
    vector<float> current_strategy_cache;
    vector<float> average_strategy_cache;
    vector<float> evs;

    bool locked_ = false;
    vector<float> locked_strategy_;
    vector<bool> locked_mask_;     // Locked per hand

public:
    PreflopTrainable(vector<PrivateCards>* private_cards, int action_number);
    virtual ~PreflopTrainable() = default;

    const vector<float>& getAverageStrategy() override;
    const vector<float>& getcurrentStrategy() override;
    void computeCurrentStrategy();
    void updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) override;
    void setEv(const vector<float>& evs) override;
    const vector<float>& getEvs() const { return evs; }
    void copyStrategy(shared_ptr<Trainable> other_trainable) override;
    json dump_strategy(bool with_state) override;
    json dump_evs() override;
    TrainableType get_type() override;

    void lockStrategy(const vector<float>& locked_strategy, const vector<bool>& locked_mask) override;
    void unlockStrategy() override;
    void load_strategy_and_evs(const vector<float>& avg_strat, const vector<float>& ev_vals);
    bool isLocked() const override;
    const vector<bool>& getLockedMask() const override;
    const vector<float>& getLockedStrategy() const { return locked_strategy_; }

private:
    const vector<float>& getcurrentStrategyNoCache();
};

#endif // TEXASSOLVER_PREFLOPTRAINABLE_H
