#ifndef GLOBALS_H

#define GLOBALS_H

#include <vector>
#include <utility>
#include <QString>

enum class SolverEngine {
    GPU_HIP = 0,
    CPU_PCFR = 1,
    GPU_CUDA = 2
};

const std::vector<std::pair<SolverEngine, QString>> SolverEngineOptions = {
    {SolverEngine::GPU_HIP, "GPU (AMD HipPCfrSolver)"},
    {SolverEngine::GPU_CUDA, "GPU (NVIDIA CudaPCfrSolver)"},
    {SolverEngine::CPU_PCFR, "CPU (PCfrSolver)"}
};

#endif
