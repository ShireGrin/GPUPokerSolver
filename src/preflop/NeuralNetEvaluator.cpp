#ifdef USE_LIBTORCH

#include "include/preflop/NeuralNetEvaluator.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <set>
#include <map>

NeuralNetEvaluator::NeuralNetEvaluator(const std::string& model_path) {
    try {
        model = torch::jit::load(model_path);
        model.eval();
        model_loaded = true;
        std::cout << "NeuralNetEvaluator: Loaded model from " << model_path << std::endl;
    } catch (const c10::Error& e) {
        std::cerr << "NeuralNetEvaluator: Failed to load model: " << e.what() << std::endl;
        model_loaded = false;
        return;
    }
    generateStratifiedFlops();
    std::cout << "NeuralNetEvaluator: Generated " << stratified_flops.size()
              << " stratified flops." << std::endl;
}

std::vector<float> NeuralNetEvaluator::cardsToVec(int c0, int c1, int c2) {
    std::vector<float> vec(52, 0.0f);
    vec[c0] = 1.0f;
    vec[c1] = 1.0f;
    vec[c2] = 1.0f;
    return vec;
}

std::string NeuralNetEvaluator::classifyFlop(int c0, int c1, int c2) {
    // Card index layout: rank = card % 13 (0=2, 12=A), suit = card / 13
    int r0 = c0 % 13, r1 = c1 % 13, r2 = c2 % 13;
    int s0 = c0 / 13, s1 = c1 / 13, s2 = c2 / 13;

    // Sort ranks descending
    int ranks[3] = {r0, r1, r2};
    std::sort(ranks, ranks + 3, std::greater<int>());
    int high = ranks[0], mid = ranks[1], low = ranks[2];

    // Suit pattern
    bool monotone = (s0 == s1 && s1 == s2);
    bool rainbow = (s0 != s1 && s1 != s2 && s0 != s2);
    // If not monotone and not rainbow, it's two-tone

    // Board is paired?
    bool paired = (r0 == r1 || r1 == r2 || r0 == r2);
    // Trips?
    bool trips = (r0 == r1 && r1 == r2);
    // Connected? (max gap between consecutive sorted ranks <= 2)
    bool connected = (!paired && (high - mid <= 2) && (mid - low <= 2));
    // High board? (highest rank >= 8, i.e., Ten or above; rank 8 = Ten in 0-indexed)
    bool high_board = (high >= 8);

    if (trips) return "trips";
    if (monotone) return "monotone";

    std::string suit_prefix = rainbow ? "rainbow" : "twotone";
    if (paired) return suit_prefix + "_paired";
    if (connected) return suit_prefix + "_connected";
    if (high_board) return suit_prefix + "_high";
    return suit_prefix + "_low";
}

void NeuralNetEvaluator::generateStratifiedFlops() {
    // Target samples per bucket (roughly proportional to natural frequency,
    // with a boost for rare textures)
    std::map<std::string, int> target_counts = {
        {"monotone",            12},
        {"twotone_high",        36},
        {"twotone_low",         36},
        {"twotone_paired",       8},
        {"twotone_connected",   18},
        {"rainbow_high",        44},
        {"rainbow_low",         44},
        {"rainbow_paired",      12},
        {"rainbow_connected",   18},
        {"trips",                2}
    };

    // Build pools of all possible flops per bucket
    std::map<std::string, std::vector<std::tuple<int,int,int>>> pools;
    for (int c0 = 0; c0 < 52; ++c0) {
        for (int c1 = c0 + 1; c1 < 52; ++c1) {
            for (int c2 = c1 + 1; c2 < 52; ++c2) {
                std::string cat = classifyFlop(c0, c1, c2);
                pools[cat].emplace_back(c0, c1, c2);
            }
        }
    }

    // Sample from each bucket
    std::mt19937 rng(42); // fixed seed for reproducibility
    stratified_flops.clear();
    for (auto& [bucket, count] : target_counts) {
        auto& pool = pools[bucket];
        if (pool.empty()) continue;
        std::shuffle(pool.begin(), pool.end(), rng);
        int n = std::min(count, (int)pool.size());
        for (int i = 0; i < n; ++i) {
            auto [c0, c1, c2] = pool[i];
            stratified_flops.push_back(cardsToVec(c0, c1, c2));
        }
    }
}

std::pair<std::vector<float>, std::vector<float>> NeuralNetEvaluator::evaluate(
    float pot, float stack, float spr,
    const std::vector<float>& oop_range,
    const std::vector<float>& ip_range,
    int64_t oop_cluster_id, int64_t ip_cluster_id,
    int64_t oop_pos_id, int64_t ip_pos_id,
    int64_t scenario_id, int64_t stack_cluster_id
) {
    std::cout << "DEBUG: NeuralNetEvaluator::evaluate() entered. pot=" << pot << ", stack=" << stack << ", spr=" << spr << std::endl;
    int num_flops = (int)stratified_flops.size();
    if (num_flops == 0 || !model_loaded) {
        // Return zeros if no model
        std::cout << "DEBUG: NeuralNetEvaluator::evaluate() early exit: no model or flops." << std::endl;
        return {std::vector<float>(1326, 0.0f), std::vector<float>(1326, 0.0f)};
    }

    torch::NoGradGuard no_grad;
    std::lock_guard<std::mutex> lock(inference_mutex);

    // Build batch tensors — one row per sampled flop, all sharing the same game state
    auto board_tensor = torch::zeros({num_flops, 52});
    auto pot_tensor = torch::zeros({num_flops, 3});
    auto oop_range_tensor = torch::zeros({num_flops, 1326});
    auto ip_range_tensor = torch::zeros({num_flops, 1326});
    auto oop_cid_tensor = torch::zeros({num_flops}, torch::kLong);
    auto ip_cid_tensor = torch::zeros({num_flops}, torch::kLong);
    auto oop_pid_tensor = torch::zeros({num_flops}, torch::kLong);
    auto ip_pid_tensor = torch::zeros({num_flops}, torch::kLong);
    auto scen_tensor = torch::zeros({num_flops}, torch::kLong);
    auto stack_cid_tensor = torch::zeros({num_flops}, torch::kLong);

    auto board_acc = board_tensor.accessor<float, 2>();
    auto pot_acc = pot_tensor.accessor<float, 2>();
    auto oop_range_acc = oop_range_tensor.accessor<float, 2>();
    auto ip_range_acc = ip_range_tensor.accessor<float, 2>();

    for (int f = 0; f < num_flops; ++f) {
        // Board one-hot
        for (int j = 0; j < 52; ++j) {
            board_acc[f][j] = stratified_flops[f][j];
        }
        // Continuous features
        pot_acc[f][0] = pot;
        pot_acc[f][1] = stack;
        pot_acc[f][2] = spr;
        // Ranges
        for (int j = 0; j < 1326; ++j) {
            board_acc[f][0] = 0.0f; // placeholder, ranges handled below
            oop_range_acc[f][j] = oop_range[j];
            ip_range_acc[f][j] = ip_range[j];
        }
        // Categorical IDs (same for all flops)
        oop_cid_tensor[f] = oop_cluster_id;
        ip_cid_tensor[f] = ip_cluster_id;
        oop_pid_tensor[f] = oop_pos_id;
        ip_pid_tensor[f] = ip_pos_id;
        scen_tensor[f] = scenario_id;
        stack_cid_tensor[f] = stack_cluster_id;
    }

    // Forward pass
    std::vector<torch::jit::IValue> inputs;
    inputs.push_back(board_tensor);
    inputs.push_back(pot_tensor);
    inputs.push_back(oop_range_tensor);
    inputs.push_back(ip_range_tensor);
    inputs.push_back(oop_cid_tensor);
    inputs.push_back(ip_cid_tensor);
    inputs.push_back(oop_pid_tensor);
    inputs.push_back(ip_pid_tensor);
    inputs.push_back(scen_tensor);
    inputs.push_back(stack_cid_tensor);

    std::cout << "DEBUG: NeuralNetEvaluator::evaluate() calling model.forward()..." << std::endl;
    auto output = model.forward(inputs).toTuple();
    std::cout << "DEBUG: NeuralNetEvaluator::evaluate() forward() completed successfully." << std::endl;
    torch::Tensor oop_evs_batch = output->elements()[0].toTensor(); // [num_flops, 1326]
    torch::Tensor ip_evs_batch = output->elements()[1].toTensor();  // [num_flops, 1326]

    // Average across all flops
    torch::Tensor oop_evs_avg = oop_evs_batch.mean(0); // [1326]
    torch::Tensor ip_evs_avg = ip_evs_batch.mean(0);   // [1326]

    // Convert to std::vector
    std::vector<float> oop_result(1326), ip_result(1326);
    auto oop_ptr = oop_evs_avg.data_ptr<float>();
    auto ip_ptr = ip_evs_avg.data_ptr<float>();
    std::copy(oop_ptr, oop_ptr + 1326, oop_result.begin());
    std::copy(ip_ptr, ip_ptr + 1326, ip_result.begin());

    return {oop_result, ip_result};
}

#endif // USE_LIBTORCH
