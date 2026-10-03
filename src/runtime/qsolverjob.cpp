#include "include/runtime/qsolverjob.h"
#include <iostream>


using namespace std;

void QSolverJob:: setContext(QSTextEdit * textEdit){
    this->textEdit = textEdit;
    //QDebugStream qout(qDebug().noquote(), this->textEdit);

}

PokerSolver* QSolverJob::get_solver(){
    if(this->mode == Mode::HOLDEM){
        return &this->ps_holdem;
    }else if(this->mode == Mode::SHORTDECK){
        return &this->ps_shortdeck;
    }else throw runtime_error("unknown mode in get_solver");
}

void QSolverJob::run()
{
    //QDebugStream qout(qDebug().noquote(), this->textEdit);
    try{
        if(this->current_mission == MissionType::SOLVING){
            this->solving();
        }else if(this->current_mission == MissionType::LOADING){
            this->loading();
        }else if(this->current_mission == MissionType::BUILDTREE){
            this->build_tree();
        }else if(this->current_mission == MissionType::SAVING){
            this->saving();
        }else if(this->current_mission == MissionType::LOAD_SOLVE){
            std::cout << "DEBUG RUN LOAD_SOLVE: range_ip size=" << this->range_ip.size() 
                      << ", range_oop size=" << this->range_oop.size() 
                      << ", board=" << this->board 
                      << ", pot=" << (ip_commit + oop_commit) 
                      << ", stack=" << stack 
                      << ", raise_limit=" << raise_limit << std::endl;
            this->build_tree();
            if(this->mode == Mode::HOLDEM){
                this->ps_holdem.buildSolverOnly(
                    this->range_ip, this->range_oop, this->board, "", max_iteration, this->print_interval, "discounted_cfr", -1, this->accuracy, this->use_isomorphism, this->use_halffloats, this->thread_number, this->engineType
                );
                this->ps_holdem.get_solver()->load_solve_from_file(this->solve_filepath);
                if (this->loaded_exploitability >= 0.0) {
                    this->ps_holdem.get_solver()->last_exploitability = this->loaded_exploitability;
                }
            }else if(this->mode == Mode::SHORTDECK){
                this->ps_shortdeck.buildSolverOnly(
                    this->range_ip, this->range_oop, this->board, "", max_iteration, this->print_interval, "discounted_cfr", -1, this->accuracy, this->use_isomorphism, this->use_halffloats, this->thread_number, this->engineType
                );
                this->ps_shortdeck.get_solver()->load_solve_from_file(this->solve_filepath);
                if (this->loaded_exploitability >= 0.0) {
                    this->ps_shortdeck.get_solver()->last_exploitability = this->loaded_exploitability;
                }
            }
        }else{
            throw runtime_error("unsupported mission type");
        }
    }
    catch (const runtime_error& error)
    {
        qDebug().noquote() << tr("Encountering error:");//.toStdString() << endl;
        qDebug().noquote() << error.what() << "\n";
    }
}

void QSolverJob::loading(){
    string suits = "c,d,h,s";
    string ranks;
    this->resource_dir =  ":/resources";
    string compairer_file, compairer_file_bin;
    int lines;
    qDebug().noquote() << tr("Loading holdem compairing file");//.toStdString() << endl;
    //if(mode == "holdem"){
    ranks = "2,3,4,5,6,7,8,9,T,J,Q,K,A";
    compairer_file = this->resource_dir + "/compairer/card5_dic_sorted.txt";
    compairer_file_bin = this->resource_dir + "/compairer/card5_dic_zipped.bin";
    //qDebug().noquote() << compairer_file_bin.c_str();
    lines = 2598961;
    this->ps_holdem = PokerSolver(ranks,suits,compairer_file,lines,compairer_file_bin);

    qDebug().noquote() << tr("Loading shortdeck compairing file");//.toStdString() << endl;
    //}else if(mode == "shortdeck"){
    ranks = "6,7,8,9,T,J,Q,K,A";
    compairer_file = this->resource_dir + "/compairer/card5_dic_sorted_shortdeck.txt";
    compairer_file_bin = this->resource_dir + "/compairer/card5_dic_zipped_shortdeck.bin";
    lines = 376993;
    this->ps_shortdeck = PokerSolver(ranks,suits,compairer_file,lines,compairer_file_bin);
    qDebug().noquote() << tr("Loading finished. Good to go.");//.toStdString() << endl;
}


void QSolverJob::saving(){
    qDebug().noquote() << tr("Saving json file..");//.toStdString() << std::endl;

    QSettings setting("TexasSolver", "Setting");
    setting.beginGroup("solver");
    this->dump_rounds = setting.value("dump_round").toInt();

    qDebug().noquote() << tr("Dump round: ") << this->dump_rounds;
    if(this->dump_rounds == 3){
        qDebug().noquote() << tr("This could be slow, or even blow your RAM, dump to river is not well optimized :(");
    }
    if(this->mode == Mode::HOLDEM){
        this->ps_holdem.dump_strategy(this->savefile,this->dump_rounds);
    }else if(this->mode == Mode::SHORTDECK){
        this->ps_shortdeck.dump_strategy(this->savefile,this->dump_rounds);
    }
    qDebug().noquote() << tr("Saving done.");//.toStdString() << std::endl;
}

void QSolverJob::stop(){
    qDebug().noquote() << tr("Trying to stop solver.");
    if(this->mode == Mode::HOLDEM){
        this->ps_holdem.stop();
    }else if(this->mode == Mode::SHORTDECK){
        this->ps_shortdeck.stop();
    }
}

void QSolverJob::solving(){
    // TODO  为什么ui上多次求解会积累memory？哪里leak了？
    // TODO  为什么有时候会莫名闪退？
    qDebug().noquote() << tr("Start Solving..");//.toStdString() << std::endl;

    this->solveStartTime = QDateTime::currentMSecsSinceEpoch();
    this->totalSolveTimeMs = 0;

    try {
        if(this->mode == Mode::HOLDEM){
            this->ps_holdem.buildSolverOnly(
                this->range_ip,
                this->range_oop,
                this->board,
                "",
                max_iteration,
                this->print_interval,
                "discounted_cfr",
                -1,
                this->accuracy,
                this->use_isomorphism,
                this->use_halffloats,
                this->thread_number,
                this->engineType
            );
            this->reapplyLockedStrategies();
            this->ps_holdem.train();
        }else if(this->mode == Mode::SHORTDECK){
            this->ps_shortdeck.buildSolverOnly(
                this->range_ip,
                this->range_oop,
                this->board,
                "",
                max_iteration,
                this->print_interval,
                "discounted_cfr",
                -1,
                this->accuracy,
                this->use_isomorphism,
                this->use_halffloats,
                this->thread_number,
                this->engineType
            );
            this->reapplyLockedStrategies();
            this->ps_shortdeck.train();
        }
    } catch (...) {
        this->totalSolveTimeMs = QDateTime::currentMSecsSinceEpoch() - this->solveStartTime;
        throw;
    }
    this->totalSolveTimeMs = QDateTime::currentMSecsSinceEpoch() - this->solveStartTime;
    qDebug().noquote() << tr("Solving done.");//.toStdString() << std::endl;
}

long long QSolverJob::estimate_tree_memory(QString range1,QString range2,QString board){
    qDebug().noquote() << tr("Estimating tree memory..");//.toStdString() << endl;
    if(this->mode == Mode::HOLDEM){
        return ps_holdem.estimate_tree_memory(range1,range2,board);
    }else if(this->mode == Mode::SHORTDECK){
        return ps_shortdeck.estimate_tree_memory(range1,range2,board);
    }
    return 0;
}

void QSolverJob::build_tree(){
    qDebug().noquote() << tr("building tree..");//.toStdString() << endl;
    if(this->mode == Mode::HOLDEM){
        ps_holdem.build_game_tree(oop_commit,ip_commit,current_round,raise_limit,small_blind,big_blind,stack,*gtbs.get(),allin_threshold);
    }else if(this->mode == Mode::SHORTDECK){
        ps_shortdeck.build_game_tree(oop_commit,ip_commit,current_round,raise_limit,small_blind,big_blind,stack,*gtbs.get(),allin_threshold);
    }
    qDebug().noquote() << tr("build tree finished");//.toStdString() << endl;
}

void QSolverJob::storeLockedStrategy(const std::string& nodeId, const std::vector<float>& strategy, const std::vector<bool>& mask) {
    NodeLockState state;
    state.strategy = strategy;
    state.mask = mask;
    this->locked_strategies[nodeId] = state;
}

void QSolverJob::clearAllLocks() {
    this->locked_strategies.clear();
}

void QSolverJob::reapplyLockedStrategies() {
    if(this->locked_strategies.empty()) return;

    auto tree = this->get_solver()->getGameTree();
    if(!tree) {
        qDebug().noquote() << "  Error: GameTree is null!";
        return;
    }

    for(const auto& kv: locked_strategies){
        const auto& key=kv.first;
        const auto& state=kv.second;

        size_t hash_pos = key.find('#');
        if(hash_pos == std::string::npos) {
            qDebug().noquote() << "    Error: Invalid key format (no '#')";
            continue;
        }

        std::string id = key.substr(0, hash_pos);
        int deal_index = std::stoi(key.substr(hash_pos + 1));

        auto node = tree->findNodeById(id);
        if(!node) {
            qDebug().noquote() << "    Error: Node not found in tree for ID:" << QString::fromStdString(id);
            continue;
        }
        if(node->getType()!=GameTreeNode::GameTreeNodeType::ACTION) {
            qDebug().noquote() << "    Error: Node is not an ACTION node!";
            continue;
        }

        auto actionNode=static_pointer_cast<ActionNode>(node);
        auto trainable=actionNode->getTrainable(deal_index,true,this->use_halffloats);
        if(!trainable) {
            qDebug().noquote() << "    Error: Trainable is null for deal_index:" << deal_index;
            continue;
        }

        try {
            trainable->lockStrategy(state.strategy, state.mask);
        } catch (const std::exception& e) {
            qDebug().noquote() << "    Warning: Failed to reapply lock on node" << QString::fromStdString(id) << ":" << e.what();
        }
    }
}
