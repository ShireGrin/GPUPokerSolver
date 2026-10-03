#-------------------------------------------------
#
# Project created by QtCreator 2021-09-18T11:22:44
#
#-------------------------------------------------

QT       += core gui sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = GPUPokerSolver
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

TRANSLATIONS =  src/lang_cn.ts\
                src/lang_en.ts


macx: {
QMAKE_CXXFLAGS += -Xpreprocessor -fopenmp -lomp -I/usr/local/include
}

macx: {
QMAKE_LFLAGS += -lomp
}

macx: {
LIBS += -L /usr/local/lib /usr/local/lib/libomp.dylib
ICON = imgs/texassolver_logo.icns
}

win32: {
QMAKE_CXXFLAGS+= -openmp
QMAKE_LFLAGS +=  -openmp
RC_ICONS = imgs/texassolver_logo.ico
}

win64: {
QMAKE_CXXFLAGS+= -openmp
QMAKE_LFLAGS +=  -openmp
RC_ICONS = imgs/texassolver_logo.ico
}

linux: {
QMAKE_CXXFLAGS += -fopenmp
QMAKE_LFLAGS += -fopenmp
 
# Explicitly check for the new official AMD ROCm compiler
system(test -f /opt/rocm/bin/hipcc) {
    message("Official AMD ROCm compiler found: enabling HIP C++ GPU Solver")
    DEFINES += USE_GPU_HIP
    
    # Define HIP compiler rule for qmake
    hipcc_compiler.name = HIP Compiler
    hipcc_compiler.input = HIP_SOURCES
    hipcc_compiler.dependency_type = TYPE_C
    hipcc_compiler.variable_out = OBJECTS
    hipcc_compiler.output = $$OUT_PWD/${QMAKE_FILE_BASE}.o
    
    hipcc_compiler.commands = /opt/rocm/bin/hipcc -c --offload-arch=native -O3 -isystem /opt/rocm/include -DUSE_GPU_HIP -D__HIP_PLATFORM_AMD__ -I$$PWD -I$$PWD/include ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
    QMAKE_EXTRA_COMPILERS += hipcc_compiler
    
    # Link with AMD HIP runtime library
    LIBS += -L/opt/rocm/lib -lamdhip64
    
    # Include HIP source files
    HIP_SOURCES += src/solver/HipKernels.hip
    SOURCES += src/solver/HipPCfrSolver.cpp
    HEADERS += include/solver/HipPCfrSolver.h
}

# Explicitly check for NVIDIA CUDA compiler
system(which nvcc > /dev/null 2>&1) {
    message("NVIDIA CUDA compiler found: enabling CUDA GPU Solver")
    DEFINES += USE_GPU_CUDA
    
    # Run the generator script to ensure CUDA files are present
    system(python3 $$PWD/scripts/generate_cuda.py)

    # Define NVCC compiler rule for qmake
    nvcc_compiler.name = NVCC Compiler
    nvcc_compiler.input = CUDA_SOURCES
    nvcc_compiler.dependency_type = TYPE_C
    nvcc_compiler.variable_out = OBJECTS
    nvcc_compiler.output = $$OUT_PWD/${QMAKE_FILE_BASE}.o
    
    nvcc_compiler.commands = nvcc -c -O3 -arch=sm_86 -std=c++14 -DUSE_GPU_CUDA -D__HIP_PLATFORM_NVIDIA__ -I$$PWD -I$$PWD/include ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
    QMAKE_EXTRA_COMPILERS += nvcc_compiler
    
    # Link with CUDA runtime
    LIBS += -L/usr/local/cuda/lib64 -lcudart
    INCLUDEPATH += /usr/local/cuda/include /opt/cuda/include
    
    # Include CUDA source files
    CUDA_SOURCES += src/solver/CudaKernels.cu
    SOURCES += src/solver/CudaPCfrSolver.cpp
    HEADERS += include/solver/CudaPCfrSolver.h
}
}

QMAKE_CXXFLAGS_RELEASE *= -O2
QMAKE_LFLAGS += -v

INCLUDEPATH += . include include/ui src src/ui

SOURCES += \
    src/main.cpp \
    src/ui/mainwindow.cpp \
    src/Deck.cpp \
    src/Card.cpp \
    src/console.cpp \
    src/GameTree.cpp \
    src/library.cpp \
    src/compairer/Dic5Compairer.cpp \
    src/experimental/TCfrSolver.cpp \
    src/nodes/ActionNode.cpp \
    src/nodes/ChanceNode.cpp \
    src/nodes/GameActions.cpp \
    src/nodes/GameTreeNode.cpp \
    src/nodes/ShowdownNode.cpp \
    src/nodes/TerminalNode.cpp \
    src/pybind/bindSolver.cpp \
    src/ranges/PrivateCards.cpp \
    src/ranges/PrivateCardsManager.cpp \
    src/ranges/RiverCombs.cpp \
    src/ranges/RiverRangeManager.cpp \
    src/runtime/PokerSolver.cpp \
    src/solver/BestResponse.cpp \
    src/solver/CfrSolver.cpp \
    src/solver/PCfrSolver.cpp \
    src/solver/Solver.cpp \
    src/tools/CommandLineTool.cpp \
    src/tools/GameTreeBuildingSettings.cpp \
    src/tools/lookup8.cpp \
    src/tools/PrivateRangeConverter.cpp \
    src/tools/dbmanager.cpp \
    src/tools/progressbar.cpp \
    src/tools/Rule.cpp \
    src/tools/StreetSetting.cpp \
    src/tools/utils.cpp \
    src/trainable/CfrPlusTrainable.cpp \
    src/trainable/DiscountedCfrTrainable.cpp \
    src/trainable/DiscountedCfrTrainableHF.cpp \
    src/trainable/DiscountedCfrTrainableSF.cpp \
    src/trainable/Trainable.cpp \
    src/runtime/qsolverjob.cpp \
    src/ui/qstextedit.cpp \
    src/ui/strategyexplorer.cpp \
    src/ui/qstreeview.cpp \
    src/ui/treeitem.cpp \
    src/ui/treemodel.cpp \
    src/ui/htmltableview.cpp \
    src/ui/worditemdelegate.cpp \
    src/ui/tablestrategymodel.cpp \
    src/ui/strategyitemdelegate.cpp \
    src/ui/detailwindowsetting.cpp \
    src/ui/detailviewermodel.cpp \
    src/ui/detailitemdelegate.cpp \
    src/ui/roughstrategyviewermodel.cpp \
    src/ui/roughstrategyitemdelegate.cpp \
    src/ui/droptextedit.cpp \
    src/ui/htmltablerangeview.cpp \
    src/ui/profileclusterexplorer.cpp \
    src/ui/rangeselector.cpp \
    src/ui/rangeselectortablemodel.cpp \
    src/ui/rangeselectortabledelegate.cpp \
    src/ui/boardselector.cpp \
    src/ui/boardselectortablemodel.cpp \
    src/ui/boardselectortabledelegate.cpp \
    src/ui/settingeditor.cpp \
    src/ui/gtotrainerwindow.cpp \
    src/ui/pt4importdialog.cpp \
    src/ui/autoevdialog.cpp \
    src/preflop/PreflopRule.cpp \
    src/preflop/PreflopIcmCalculator.cpp \
    src/preflop/PreflopEquityManager.cpp \
    src/preflop/PreflopGameTree.cpp \
    src/preflop/PreflopCfrSolver.cpp \
    src/preflop/PreflopTrainable.cpp \
    src/preflop/preflopsolverwindow.cpp \
    src/ui/handhistoryparser.cpp \
    src/ui/handvisualizerwidget.cpp


HEADERS += \
    include/tools/half-1-12-0.h \
    include/trainable/DiscountedCfrTrainableHF.h \
    include/trainable/DiscountedCfrTrainableSF.h \
    include/ui/mainwindow.h \
    include/Card.h \
    include/GameTree.h \
    include/Deck.h \
    include/json.hpp \
    include/library.h \
    include/solver/PCfrSolver.h \
    include/solver/Solver.h \
    include/solver/BestResponse.h \
    include/solver/CfrSolver.h \
    include/tools/argparse.hpp \
    include/tools/CommandLineTool.h \
    include/tools/utils.h \
    include/tools/GameTreeBuildingSettings.h \
    include/tools/Rule.h \
    include/tools/StreetSetting.h \
    include/tools/lookup8.h \
    include/tools/PrivateRangeConverter.h \
    include/tools/dbmanager.h \
    include/tools/progressbar.h \
    include/runtime/PokerSolver.h \
    include/trainable/CfrPlusTrainable.h \
    include/trainable/DiscountedCfrTrainable.h \
    include/trainable/Trainable.h \
    include/compairer/Compairer.h \
    include/compairer/Dic5Compairer.h \
    include/experimental/TCfrSolver.h \
    include/nodes/ActionNode.h \
    include/nodes/ChanceNode.h \
    include/nodes/GameActions.h \
    include/nodes/GameTreeNode.h \
    include/nodes/ShowdownNode.h \
    include/nodes/TerminalNode.h \
    include/ranges/PrivateCards.h \
    include/ranges/PrivateCardsManager.h \
    include/ranges/RiverCombs.h \
    include/ranges/RiverRangeManager.h \
    include/tools/tinyformat.h \
    include/tools/qdebugstream.h \
    include/runtime/qsolverjob.h \
    include/ui/qstextedit.h \
    include/ui/strategyexplorer.h \
    include/ui/qstreeview.h \
    include/ui/treeitem.h \
    include/ui/treemodel.h \
    include/ui/htmltableview.h \
    include/ui/worditemdelegate.h \
    include/ui/tablestrategymodel.h \
    include/ui/strategyitemdelegate.h \
    include/ui/detailwindowsetting.h \
    include/ui/detailviewermodel.h \
    include/ui/detailitemdelegate.h \
    include/ui/roughstrategyviewermodel.h \
    include/ui/roughstrategyitemdelegate.h \
    include/ui/droptextedit.h \
    include/ui/htmltablerangeview.h \
    include/ui/profileclusterexplorer.h \
    include/ui/rangeselector.h \
    include/ui/rangeselectortablemodel.h \
    include/ui/rangeselectortabledelegate.h \
    include/ui/boardselector.h \
    include/ui/boardselectortablemodel.h \
    include/ui/boardselectortabledelegate.h \
    include/ui/settingeditor.h \
    include/ui/gtotrainerwindow.h \
    include/ui/pt4importdialog.h \
    include/ui/autoevdialog.h \
    include/preflop/PreflopRule.h \
    include/preflop/PreflopIcmCalculator.h \
    include/preflop/PreflopEquityManager.h \
    include/preflop/PreflopGameTree.h \
    include/preflop/PreflopGameTreeNode.h \
    include/preflop/PreflopCfrSolver.h \
    include/preflop/PreflopTrainable.h \
    include/preflop/preflopsolverwindow.h \
    include/ui/handhistoryparser.h \
    include/ui/handvisualizerwidget.h

FORMS += \
    src/ui/mainwindow.ui \
    src/ui/strategyexplorer.ui \
    src/ui/rangeselector.ui \
    src/ui/boardselector.ui \
    src/ui/settingeditor.ui \
    src/ui/gtotrainerwindow.ui \
    src/ui/pt4importdialog.ui \
    src/ui/autoevdialog.ui \
    src/preflop/preflopsolverwindow.ui


RESOURCES += \
    src/translations.qrc \
    src/compairer.qrc

LIBS += -lz

# ============================================================
# Neural Network support via LibTorch (optional)
# Usage: qmake "LIBTORCH_PATH=/path/to/libtorch" "USE_LIBTORCH=true"
# ============================================================
equals(USE_LIBTORCH, true) {
    message("LibTorch support ENABLED at $$LIBTORCH_PATH")
    DEFINES += USE_LIBTORCH

    INCLUDEPATH += $$LIBTORCH_PATH/include
    INCLUDEPATH += $$LIBTORCH_PATH/include/torch/csrc/api/include

    LIBS += -L$$LIBTORCH_PATH/lib
    LIBS += -ltorch -ltorch_cpu -lc10 -lgomp
    QMAKE_RPATHDIR += $$LIBTORCH_PATH/lib

    HEADERS += include/preflop/NeuralNetEvaluator.h
    SOURCES += src/preflop/NeuralNetEvaluator.cpp
} else {
    message("LibTorch support DISABLED (no neural net inference)")
}
