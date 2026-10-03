#define __HIP_PLATFORM_NVIDIA__ 
#include <cuda_runtime.h> // Add this include
#pragma once
#include "include/solver/PCfrSolver.h"
#include <include/ranges/PrivateCards.h>
#include <include/compairer/Compairer.h>
#include <include/Deck.h>
#include <include/ranges/RiverRangeManager.h>
#include <include/ranges/PrivateCardsManager.h>
#include <include/trainable/DiscountedCfrTrainable.h>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <queue>

#include "include/solver/GpuTypes.h"

class CudaPCfrSolver : public PCfrSolver {
    public:
        CudaPCfrSolver(
            std::shared_ptr<GameTree> tree,
            std::vector<PrivateCards> range1,
            std::vector<PrivateCards> range2,
            std::vector<int> initial_board,
            std::shared_ptr<Compairer> compairer,
            Deck deck,
            int iteration_number,
            int print_interval,
            std::string logfile,
            std::string trainer,
            float accuracy,
            bool use_fp16
        );
        virtual ~CudaPCfrSolver();

        void train() override;
        void stop() override;

    private:
        void buildGpuStates();
        void printExpectedVramUsage();
        void setupShowdowns();
        void allocateDeviceMemory();
        void runCfrIteration(int iter);
        void copyToDevice();
        void copyFromDevice();
        void freeDeviceMemory();
        void syncStrategiesToCpuTree();
        void calculateEvs();

        bool use_fp16 = true;

        // Flat state and memory maps
        std::vector<GpuState> h_states;

        // Maps (ActionNode pointer, deal_id) -> state index
        std::map<std::pair<ActionNode*, int>, int> action_state_map;

        // Total size of regrets/strategies flat buffer
        int total_trainable_size = 0;

        // Level parallelization
        std::vector<std::pair<int, int>> h_levels;
        int num_levels = 0;

        // Host memory for regrets and strategies
        std::vector<float> h_regrets;
        std::vector<float> h_strategies;
        std::vector<float> h_cum_strategies;
        std::vector<uint8_t> h_locked;

        // Device pointers
        GpuState* d_states = nullptr;
        void* d_regrets = nullptr;         // float* or __half*
        void* d_strategies = nullptr;      // float* or __half*
        void* d_cum_strategies = nullptr;  // float* or __half*
        uint8_t* d_locked = nullptr;
        void* d_reach_probs = nullptr;    // float array of size: num_states * max(range1_size, range2_size)
        void* d_utilities = nullptr;      // float array of size: num_states * max(range1_size, range2_size)


        std::vector<ShowdownPrecalc> h_showdowns;
        std::vector<GpuShowdownComb> h_showdown_combs;

        ShowdownPrecalc* d_showdowns = nullptr;
        GpuShowdownComb* d_showdown_combs = nullptr;

        // Card data for blocker calculations
        GpuRangeCard* d_range_cards_0 = nullptr;
        GpuRangeCard* d_range_cards_1 = nullptr;

        int* d_p0_to_p1_map = nullptr;
        int* d_p1_to_p0_map = nullptr; 


    protected:
        cudaStream_t compute_stream = nullptr;

};
