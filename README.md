# GPUPokerSolver - Advanced GPU & Neural Network Edition

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![C++14](https://img.shields.io/badge/C%2B%2B-14-purple.svg)](https://isocpp.org/)
[![Qt5](https://img.shields.io/badge/Qt-5.15+-green.svg)](https://www.qt.io/)
[![Build: CMake/QMake](https://img.shields.io/badge/Build-QMake-orange.svg)]()

## 📺 Video Demo

VIDEO

## 🚀 Introduction 
This is a massively upgraded, heavily refactored, and hardware-accelerated fork of the original open-source [TexasSolver](https://github.com/bupticybee/TexasSolver). 

While the original solver relied strictly on CPU processing, this version has been completely re-architected to support **Native GPU Acceleration (AMD ROCm/HIP & NVIDIA CUDA)** and deep **Neural Network evaluations via LibTorch**. By offloading heavy Counterfactual Regret Minimization (CFR) calculations to the GPU and utilizing PyTorch value networks to evaluate tree depths, this solver achieves exponential speedups and dramatically lower memory footprints compared to traditional CPU solvers.

### ✨ Key Upgrades in this Version
- 🚀 **Hardware Acceleration:** Full integration for both AMD (ROCm/HIP) and NVIDIA (CUDA) graphics cards.
- 🧠 **Neural Network Pipeline:** Uses traced PyTorch models (`value_network_traced.pt`) for game tree leaf evaluation, reducing the need to solve deep trees manually.
- 🏗️ **Clean Architecture:** Completely restructured into a modern, professional C++ layout (`src/`, `include/`) enforcing strict out-of-source builds.
- ⚙️ **Centralized Configuration:** Secure `.env` credential management supporting dynamic PostgreSQL databases and customized player filters.
- 🧹 **Debloated History:** Repository history has been purged of large binaries and cached weights for lightning-fast cloning.
- 🖥️ **Integrated Qt GUI:** A fully featured frontend for range selection, tree building, and strategy exploration.
🛠️️ Build Instructions
This project enforces a clean out-of-source build using qmake.

Prerequisites
Qt Framework: Qt 5 or Qt 6 (with qtdeclarative and qtwebsockets)

Compiler: GCC/Clang with C++14 support

GPU Toolkit (Optional but recommended):

AMD: ROCm installed at /opt/rocm

NVIDIA: CUDA Toolkit installed at /usr/local/cuda

LibTorch (Optional): Required if evaluating via Neural Networks.

## Compilation Steps
### 1. Clone the repository
```
git clone [https://github.com/ShireGrin/GPUPokerSolver.git](https://github.com/ShireGrin/GPUPokerSolver.git)
cd GPUPokerSolver
```

### 2. Create the build directory

```
mkdir build
cd build
```


### 3. Generate the Makefile
```
qmake ../GPUPokerSolver.pro
```

### 3.1 Python Environment Setup

1. **Install PyTorch** depending on your hardware:
   - **AMD (ROCm):** 
     ```bash
     pip install torch torchvision --index-url [https://download.pytorch.org/whl/rocm6.0](https://download.pytorch.org/whl/rocm7.2)
     ```
   - **NVIDIA (CUDA):** 
     ```bash
     pip install torch torchvision --index-url [https://download.pytorch.org/whl/cu121](https://download.pytorch.org/whl/cu121)
     ```
   - **CPU Only:** 
     ```bash
     pip install torch torchvision
     ```

2. **Install remaining dependencies:**
   ```bash
   pip install -r requirements.txt
   ```


### 4. Compile using all available CPU cores
```
make -j$(nproc)
```

## ⚙️ Configuration (.env Setup)
This project uses a centralized .env file for database credentials and UI defaults rather than hardcoded strings.

Copy the example file in the root directory:
```
cp .env.example .env
```

Edit .env to match your PostgreSQL setup and preferred player filters:
```
# For Pokertracker4 DB
DB_PT4="" # Your Poker Tracker 4 DB Name
DB_PT4_USER=postgres
DB_PT4_PASSWORD=
DB_PT4_HOST=localhost
DB_PT4_PORT=5432

# For GPUPokerSolver DB
DB_SOLVER=dbname
DB_SOLVER_USER=postgres
DB_SOLVER_PASSWORD=
DB_SOLVER_HOST=localhost
DB_SOLVER_PORT=5432

# For Usage
DEFAULT_PLAYERS="Nickname1, Nickname2" # The poker player nickname on site
PT4_IMPORT_DATABASES="PT4DB" #Probably just like DB_PT4, it's where you'll import hands from
```

## 🧠 Model Weights
To use the Neural Network features, place your trained PyTorch weights (value_network.pth / value_network_traced.pt) directly into your build/ folder alongside the compiled executable.

(Note: Check the GitHub Releases page to download the latest pre-trained model weights).

## 🚀 Usage
Always run the application using your run.sh wrapper script, which automatically loads your .env variables into the environment:

```
./run.sh
```
Use the graphical interface to configure your SPR, input preflop ranges, and build the tree. The solver will automatically detect your hardware backend (CPU, HIP, or CUDA) based on how it was compiled.

## ⚖️ License & Acknowledgements
This project is licensed under the GNU AGPL v3.

Original Architecture: Built upon the foundational logic of the original TexasSolver by bupticybee.

Neural Networks: PyTorch / LibTorch.

GPU Backend: AMD ROCm and NVIDIA CUDA Toolkits.

If you integrate the code of this solver into your software or provide services through the internet, you must comply with the AGPL-v3 license terms.
