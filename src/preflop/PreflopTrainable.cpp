#include "include/preflop/PreflopTrainable.h"
#include "include/tools/tinyformat.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

PreflopTrainable::PreflopTrainable(vector<PrivateCards>* private_cards, int action_number) {
    this->private_cards = private_cards;
    this->action_number = action_number;
    this->hand_number = private_cards->size();

    this->evs = vector<float>(action_number * hand_number, 0.0f);
    this->r_plus = vector<float>(action_number * hand_number, 0.0f);
    this->r_plus_sum = vector<float>(hand_number, 0.0f);
    this->cum_r_plus = vector<float>(action_number * hand_number, 0.0f);

    this->current_strategy_cache = vector<float>(action_number * hand_number, 0.0f);
    this->average_strategy_cache = vector<float>(action_number * hand_number, 0.0f);
}

const vector<float>& PreflopTrainable::getAverageStrategy() {
    if (locked_ && locked_mask_.empty()) return locked_strategy_;
    
    for (int private_id = 0; private_id < hand_number; private_id++) {
        float r_plus_sum_val = 0.0f;
        for (int action_id = 0; action_id < action_number; action_id++) {
            int index = action_id * hand_number + private_id;
            r_plus_sum_val += cum_r_plus[index];
        }

        for (int action_id = 0; action_id < action_number; action_id++) {
            int index = action_id * hand_number + private_id;
            if (r_plus_sum_val > 0.0f) {
                average_strategy_cache[index] = cum_r_plus[index] / r_plus_sum_val;
            } else {
                average_strategy_cache[index] = 1.0f / action_number;
            }
        }
    }
    
    if (locked_) {
        for (int private_id = 0; private_id < hand_number; private_id++) {
            if (locked_mask_[private_id]) {
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * hand_number + private_id;
                    average_strategy_cache[index] = locked_strategy_[index];
                }
            }
        }
    }
    return average_strategy_cache;
}

const vector<float>& PreflopTrainable::getcurrentStrategy() {
    return current_strategy_cache;
}

void PreflopTrainable::computeCurrentStrategy() {
    if (locked_) {
        getcurrentStrategyNoCache();
        for (int private_id = 0; private_id < hand_number; private_id++) {
            if (locked_mask_.empty() || locked_mask_[private_id]) {
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * hand_number + private_id;
                    current_strategy_cache[index] = locked_strategy_[index];
                }
            }
        }
        return;
    }
    getcurrentStrategyNoCache();
}

const vector<float>& PreflopTrainable::getcurrentStrategyNoCache() {
    if (r_plus_sum.empty()) {
        fill(current_strategy_cache.begin(), current_strategy_cache.end(), 1.0f / action_number);
    } else {
        float epsilon = 1e-5f; // trembling hand
        for (int private_id = 0; private_id < hand_number; private_id++) {
            if (r_plus_sum[private_id] > 0.0f) {
                float sum_prob = 0.0f;
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * hand_number + private_id;
                    float prob = std::max(0.0f, r_plus[index]) / r_plus_sum[private_id];
                    current_strategy_cache[index] = std::max(epsilon, prob);
                    sum_prob += current_strategy_cache[index];
                }
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * hand_number + private_id;
                    current_strategy_cache[index] /= sum_prob;
                }
            } else {
                for (int action_id = 0; action_id < action_number; action_id++) {
                    int index = action_id * hand_number + private_id;
                    current_strategy_cache[index] = 1.0f / action_number;
                }
            }
        }
    }
    return current_strategy_cache;
}

void PreflopTrainable::updateRegrets(const vector<float>& regrets, int iteration_number, const vector<float>& reach_probs) {
    (void)reach_probs;
    if (locked_ && locked_mask_.empty()) return;

    // Standard Discounted CFR parameters: alpha = 1.5, beta = 0.5, gamma = 2, theta = 0.9
    float alpha = 1.5f;
    float beta = 0.5f;
    float gamma = 2.0f;
    float theta = 0.9f;

    float alpha_coef = std::pow(iteration_number, alpha);
    alpha_coef = alpha_coef / (1.0f + alpha_coef);

    fill(r_plus_sum.begin(), r_plus_sum.end(), 0.0f);

    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < hand_number; private_id++) {
            if (locked_ && !locked_mask_.empty() && locked_mask_[private_id]) continue;
            int index = action_id * hand_number + private_id;
            float one_reg = regrets[index];

            r_plus[index] = one_reg + r_plus[index];
            if (r_plus[index] > 0.0f) {
                r_plus[index] *= alpha_coef;
            } else {
                r_plus[index] *= beta;
            }

            r_plus_sum[private_id] += std::max(0.0f, r_plus[index]);
        }
    }

    const vector<float>& current_strategy = getcurrentStrategyNoCache();
    float strategy_coef = std::pow(((float)iteration_number / (iteration_number + 1)), gamma);
    for (int action_id = 0; action_id < action_number; action_id++) {
        for (int private_id = 0; private_id < hand_number; private_id++) {
            if (locked_ && !locked_mask_.empty() && locked_mask_[private_id]) continue;
            int index = action_id * hand_number + private_id;
            cum_r_plus[index] *= theta;
            cum_r_plus[index] += current_strategy[index] * strategy_coef;
        }
    }
}

void PreflopTrainable::setEv(const vector<float>& new_evs) {
    if (new_evs.size() != evs.size()) throw std::runtime_error("size mismatch in PreflopTrainable setEv");
    for (size_t i = 0; i < new_evs.size(); i++) {
        if (new_evs[i] == new_evs[i]) evs[i] = new_evs[i];
    }
}

void PreflopTrainable::copyStrategy(shared_ptr<Trainable> other_trainable) {
    auto trainable = dynamic_pointer_cast<PreflopTrainable>(other_trainable);
    if (trainable) {
        r_plus.assign(trainable->r_plus.begin(), trainable->r_plus.end());
        cum_r_plus.assign(trainable->cum_r_plus.begin(), trainable->cum_r_plus.end());
    }
}

json PreflopTrainable::dump_strategy(bool with_state) {
    if (with_state) throw std::runtime_error("state storage not implemented");

    json strategy;
    const vector<float>& average_strategy = getAverageStrategy();

    for (int i = 0; i < hand_number; i++) {
        PrivateCards& one_private_card = (*private_cards)[i];
        vector<float> one_strategy(action_number);

        for (int j = 0; j < action_number; j++) {
            size_t strategy_index = j * hand_number + i;
            one_strategy[j] = average_strategy[strategy_index];
        }
        strategy[tfm::format("%s", one_private_card.toString())] = one_strategy;
    }

    json retjson;
    retjson["strategy"] = std::move(strategy);
    return retjson;
}

json PreflopTrainable::dump_evs() {
    json evs_json;
    for (int i = 0; i < hand_number; i++) {
        PrivateCards& one_private_card = (*private_cards)[i];
        vector<float> one_evs(action_number);

        for (int j = 0; j < action_number; j++) {
            size_t evs_index = j * hand_number + i;
            one_evs[j] = evs[evs_index];
        }
        evs_json[tfm::format("%s", one_private_card.toString())] = one_evs;
    }

    json retjson;
    retjson["evs"] = std::move(evs_json);
    return retjson;
}

Trainable::TrainableType PreflopTrainable::get_type() {
    return DISCOUNTED_CFR_TRAINABLE;
}

void PreflopTrainable::lockStrategy(const vector<float>& locked_strategy, const vector<bool>& locked_mask) {
    if ((int)locked_strategy.size() != action_number * hand_number) {
        throw std::runtime_error("locked_strategy size does not match");
    }
    locked_strategy_ = locked_strategy;
    locked_mask_ = locked_mask;
    locked_ = true;
}

void PreflopTrainable::unlockStrategy() {
    locked_ = false;
    locked_strategy_.clear();
    locked_mask_.clear();
}

void PreflopTrainable::load_strategy_and_evs(const vector<float>& avg_strat, const vector<float>& ev_vals) {
    cum_r_plus = avg_strat;
    evs = ev_vals;
    std::fill(average_strategy_cache.begin(), average_strategy_cache.end(), 0.0f);
}

bool PreflopTrainable::isLocked() const {
    return locked_;
}

const vector<bool>& PreflopTrainable::getLockedMask() const {
    return locked_mask_;
}
