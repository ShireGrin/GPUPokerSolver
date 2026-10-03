#ifndef TEXASSOLVER_NEURALNETEVALUATOR_H
#define TEXASSOLVER_NEURALNETEVALUATOR_H

#ifdef USE_LIBTORCH

#ifdef slots
#pragma push_macro("slots")
#undef slots
#define slots_was_defined
#endif

#ifdef signals
#pragma push_macro("signals")
#undef signals
#define signals_was_defined
#endif

#include <torch/script.h>
#include <torch/torch.h>

#ifdef slots_was_defined
#pragma pop_macro("slots")
#undef slots_was_defined
#endif

#ifdef signals_was_defined
#pragma pop_macro("signals")
#undef signals_was_defined
#endif

#include <string>
#include <vector>
#include <utility>
#include <mutex>

class NeuralNetEvaluator {
public:
    // Constructor: loads the TorchScript model from the given path.
    // Generates the stratified flop set at construction time.
    explicit NeuralNetEvaluator(const std::string& model_path);

    // Evaluate postflop EVs for a preflop showdown node.
    // Runs the NN across all stratified flops in one batched forward pass,
    // then averages the results.
    //
    // Parameters:
    //   pot, stack, spr          — continuous game-state scalars
    //   oop_range, ip_range      — 1326-element vectors (reach probabilities)
    //   oop_cluster_id, ip_cluster_id, oop_pos_id, ip_pos_id,
    //   scenario_id, stack_cluster_id — categorical IDs for embeddings
    //
    // Returns: pair of vectors, each length 1326.
    //   first  = averaged OOP EVs
    //   second = averaged IP EVs
    std::pair<std::vector<float>, std::vector<float>> evaluate(
        float pot, float stack, float spr,
        const std::vector<float>& oop_range,
        const std::vector<float>& ip_range,
        int64_t oop_cluster_id, int64_t ip_cluster_id,
        int64_t oop_pos_id, int64_t ip_pos_id,
        int64_t scenario_id, int64_t stack_cluster_id
    );

    bool isLoaded() const { return model_loaded; }

private:
    torch::jit::script::Module model;
    bool model_loaded = false;

    // The pre-generated stratified flop set.
    // Each entry is a 52-dim float vector (one-hot encoded board cards).
    std::vector<std::vector<float>> stratified_flops;

    // Mutex for thread safety during inference
    std::mutex inference_mutex;

    // Generate the stratified flop set (~230 flops).
    void generateStratifiedFlops();

    // Helper: convert 3 card indices (0-51) to a 52-dim one-hot vector.
    static std::vector<float> cardsToVec(int c0, int c1, int c2);

    // Helper: classify a flop into a texture bucket.
    // Returns a string like "monotone", "twotone_high", "rainbow_paired", etc.
    static std::string classifyFlop(int c0, int c1, int c2);
};

#endif // USE_LIBTORCH
#endif // TEXASSOLVER_NEURALNETEVALUATOR_H
