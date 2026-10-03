//
// Created by bytedance on 7.6.21.
//
#include "include/tools/CommandLineTool.h"
#include <QString>
#include <map>
#include <sstream>
#include "include/nodes/ActionNode.h"
#include "include/nodes/ChanceNode.h"
#include "include/GameTree.h"
#include "include/tools/utils.h"

CommandLineTool::CommandLineTool(string mode,string resource_dir) {
    string suits = "c,d,h,s";
    string ranks;
    this->resource_dir = resource_dir;
    string compairer_file,compairer_file_bin;
    int lines;
    if(mode == "holdem"){
        ranks = "2,3,4,5,6,7,8,9,T,J,Q,K,A";
        compairer_file = this->resource_dir + "/compairer/card5_dic_sorted.txt";
        compairer_file_bin = this->resource_dir + "/compairer/card5_dic_zipped.bin";
        lines = 2598961;
    }else if(mode == "shortdeck"){
        ranks = "6,7,8,9,T,J,Q,K,A";
        compairer_file = this->resource_dir + "/compairer/card5_dic_sorted_shortdeck.txt";
        compairer_file_bin = this->resource_dir + "/compairer/card5_dic_zipped_shortdeck.bin";
        lines = 376993;
    }else{
        throw runtime_error(tfm::format("mode not recognized : ",mode));
    }
    string logfile_name = "../resources/outputs/outputs_log.txt";
    this->ps = PokerSolver(ranks,suits,compairer_file,lines,compairer_file_bin);

    StreetSetting gbs_flop_ip = StreetSetting(vector<float>{},vector<float>{},vector<float>{},true);
    StreetSetting gbs_turn_ip = StreetSetting(vector<float>{},vector<float>{},vector<float>{},true);
    StreetSetting gbs_river_ip = StreetSetting(vector<float>{},vector<float>{},vector<float>{},true);

    StreetSetting gbs_flop_oop = StreetSetting(vector<float>{},vector<float>{},vector<float>{},true);
    StreetSetting gbs_turn_oop = StreetSetting(vector<float>{},vector<float>{},vector<float>{},true);
    StreetSetting gbs_river_oop = StreetSetting(vector<float>{},vector<float>{},vector<float>{},true);

    this->gtbs = make_shared<GameTreeBuildingSettings>(gbs_flop_ip,gbs_turn_ip,gbs_river_ip,gbs_flop_oop,gbs_turn_oop,gbs_river_oop);

    this->engine = SolverEngine::CPU_PCFR;

#ifdef USE_GPU_CUDA
    if (has_nvidia_gpu()) {
        this->engine = SolverEngine::GPU_CUDA;
    }
#endif

#ifdef USE_GPU_HIP
    if (this->engine == SolverEngine::CPU_PCFR && has_amd_gpu()) {
        this->engine = SolverEngine::GPU_HIP;
    }
#endif
}

void CommandLineTool::startWorking() {
    string input_line;
    while(cin) {
        getline(cin, input_line);
        this->processCommand(input_line);
    };
}

void CommandLineTool::execFromFile(string input_file){
    std::ifstream infile(input_file);
    std::string input_line;
    while (std::getline(infile, input_line))
    {
        this->processCommand(input_line);
    }

}

void split(const string& s, char c,
           vector<string>& v) {
    string::size_type i = 0;
    string::size_type j = s.find(c);

    while (j != string::npos) {
        v.push_back(s.substr(i, j-i));
        i = ++j;
        j = s.find(c, j);

        if (j == string::npos)
            v.push_back(s.substr(i, s.length()));
    }
}


void CommandLineTool::processCommand(string input) {
    vector<string> contents;
    split(input,' ',contents);
    if(contents.size() == 0) contents = {input};
    if(contents.size() > 2 || contents.size() < 1)throw runtime_error(tfm::format("command not valid: %s",input));
    string command = contents[0];
    string paramstr = contents.size() == 1 ? "" : contents[1];
    if(command == "set_pot"){
        this->ip_commit = stof(paramstr) / 2;
        this->oop_commit = stof(paramstr) / 2;
    }else if(command == "set_effective_stack"){
        this->stack = stof(paramstr) + this->ip_commit;
    }else if(command == "set_board"){
        this->board = paramstr;
        vector<string> board_str_arr = string_split(board,',');
        if(board_str_arr.size() == 3){
            this->current_round = 1;
        }else if(board_str_arr.size() == 4){
            this->current_round = 2;
        }else if(board_str_arr.size() == 5){
            this->current_round = 3;
        }else{
            throw runtime_error(tfm::format("board %s not recognized",this->board));
        }
    }else if(command == "set_range_ip"){
        this->range_ip = paramstr;
    }else if(command == "set_range_oop"){
        this->range_oop = paramstr;
    }else if(command == "set_bet_sizes"){
        vector<string> params;
        split(paramstr,',',params);
        if(params.size() < 3)throw runtime_error("param number error");
        // oop,turn,bet,30,70,100
        string player = params[0];
        string round = params[1];
        string bet_type = params[2];
        StreetSetting& streetSetting = this->gtbs->get_setting(player,round);
        vector<float>* sizes;
        if(bet_type == "allin") streetSetting.allin = true;
        else if(bet_type == "bet") sizes = &(streetSetting.bet_sizes);
        else if(bet_type == "raise") sizes = &(streetSetting.raise_sizes);
        else if(bet_type == "donk") sizes = &(streetSetting.donk_sizes);
        else throw runtime_error("");

        if(bet_type == "bet" || bet_type == "raise" || bet_type == "donk"){
            sizes->clear();
            for(std::size_t i = 3;i < params.size();i ++ ){
                string p = params[i];
                while(!p.empty() && isspace((unsigned char)p.front())) p.erase(p.begin());
                while(!p.empty() && isspace((unsigned char)p.back())) p.pop_back();
                if(p.empty()) continue;
                for(char &c : p) c = tolower((unsigned char)c);
                if(p.back() == 'x'){
                    sizes->push_back(stof(p.substr(0, p.size() - 1)) * 100);
                }else if(p.back() == 'c' || p.back() == 'f'){
                    sizes->push_back(-stof(p.substr(0, p.size() - 1)));
                }else{
                    sizes->push_back(stof(p));
                }
            }
        }
    }else if(command == "set_accuracy"){
        this->accuracy = stof(paramstr);
    }else if(command == "set_allin_threshold"){
        this->allin_threshold = stof(paramstr);
    }else if(command == "set_thread_num"){
        this->thread_number = stoi(paramstr);
    }else if(command == "build_tree"){
        this->ps.build_game_tree(oop_commit,ip_commit,current_round,raise_limit,small_blind,big_blind,stack,*gtbs.get(),allin_threshold);
    }else if(command == "set_max_iteration"){
        this->max_iteration = stoi(paramstr);
    }else if(command == "set_use_isomorphism"){
        this->use_isomorphism = stoi(paramstr);
    }else if(command == "set_use_fp16"){
        this->use_fp16 = stoi(paramstr);
    }else if(command == "set_print_interval"){
        this->print_interval = stoi(paramstr);
    }else if(command == "set_solver"){
        if(paramstr == "cpu") this->engine = SolverEngine::CPU_PCFR;
        else if(paramstr == "gpu_cuda") this->engine = SolverEngine::GPU_CUDA;
        else if(paramstr == "gpu_hip") this->engine = SolverEngine::GPU_HIP;
        else cout << "Unknown solver engine: " << paramstr << ". Available options: cpu, gpu_cuda, gpu_hip" << endl;
    }else if(command == "start_solve"){
        cout << "<<<START SOLVING>>>" << endl;
        this->ps.buildSolverOnly(
                this->range_ip,
                this->range_oop,
                this->board,
                "tmp_log.txt",
                max_iteration,
                this->print_interval,
                "discounted_cfr",
                -1,
                this->accuracy,
                this->use_isomorphism,
                this->use_fp16,
                this->thread_number,
                this->engine
        );
        this->ps.train(
        );
    }else if(command == "dump_result"){
        string output_file = paramstr;
        this->ps.dump_strategy(QString::fromStdString(output_file),this->dump_rounds);
    }else if(command == "dump_evs"){
        string output_file = paramstr;
        shared_ptr<GameTreeNode> root = this->ps.get_game_tree()->getRoot();
        if(root->getType() == GameTreeNode::ACTION) {
            shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(root);
            shared_ptr<Solver> solver = this->ps.get_solver();
            if(solver) {
                vector<vector<vector<float>>> evs = solver->get_evs(action_node, {});
                json evs_json = evs;
                ofstream ofs(output_file);
                if(ofs.is_open()) {
                    ofs << evs_json.dump();
                    ofs.close();
                    cout << "Saved EVs to " << output_file << endl;
                } else {
                    cout << "Error: cannot open file for writing: " << output_file << endl;
                }
            } else {
                cout << "Error: solver not found." << endl;
            }
        } else {
            cout << "Error: root node is not an action node." << endl;
        }
    }else if(command == "dump_training_sample"){
        vector<string> params;
        split(paramstr,',',params);
        if(params.size() < 3){
            cout << "Usage: dump_training_sample <row_id>,<exploitability>,<solve_duration_sec>" << endl;
            return;
        }
        int row_id = stoi(params[0]);
        float exploitability = stof(params[1]);
        float solve_duration_sec = stof(params[2]);
        
        shared_ptr<Solver> solver = this->ps.get_solver();
        if(solver) {
            auto pcfr = std::dynamic_pointer_cast<PCfrSolver>(solver);
            if(pcfr) {
                pcfr->dump_training_sample(row_id, exploitability, solve_duration_sec);
                cout << "Successfully dumped training sample to database." << endl;
            } else {
                cout << "Error: solver is not PCfrSolver." << endl;
            }
        } else {
            cout << "Error: solver not found." << endl;
        }
    }else if(command == "set_dump_rounds"){
        this->dump_rounds = stoi(paramstr);
    }else if(command == "print_tree"){
        // Print all action node IDs so the user can see which nodes exist
        int depth = paramstr.empty() ? 4 : stoi(paramstr);
        this->ps.get_game_tree()->printTree(depth);
    }else if(command == "print_node_ids"){
        // Print all action node IDs with their node path
        this->printNodeIds(this->ps.get_game_tree()->getRoot(), 0);
    }else if(command == "dump_node"){
        // dump_node <node_id>,<output_file.csv>
        vector<string> params;
        split(paramstr,',',params);
        if(params.size() < 2){
            cout << "Usage: dump_node <node_id>,<output_file.csv>" << endl;
            return;
        }
        string node_id = params[0];
        string output_file = params[1];
        this->dumpNodeStrategy(node_id, output_file);
    }else if(command == "load_node_lock"){
        // load_node_lock <node_id>,<input_file.csv>
        vector<string> params;
        split(paramstr,',',params);
        if(params.size() < 2){
            cout << "Usage: load_node_lock <node_id>,<input_file.csv>" << endl;
            return;
        }
        string node_id = params[0];
        string input_file = params[1];
        this->loadNodeLock(node_id, input_file);
    }else if(command == "clear_node_lock"){
        // clear_node_lock <node_id>
        string node_id = paramstr;
        this->clearNodeLock(node_id);
    }else if(command == "set_raise_limit"){
        this->raise_limit = stoi(paramstr);
    }else if(command == "set_preselected_cards"){
        // Preselected cards are just for the UI strategy explorer to jump to a specific node.
        // We can safely ignore them in the headless solver.
    }else{
        cout << "command not recognized: " << command << endl;
    }
}

void CommandLineTool::printNodeIds(const shared_ptr<GameTreeNode>& node, int depth) {
    if(!node) return;

    string prefix;
    for(int i = 0; i < depth; i++) prefix += "  ";

    if(node->getType() == GameTreeNode::ACTION) {
        shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(node);
        string player_str = action_node->getPlayer() == 0 ? "IP" : "OOP";
        cout << prefix << "[ACTION " << player_str << "] " << node->getNodeId() << endl;

        // Print available actions
        vector<GameActions>& actions = action_node->getActions();
        cout << prefix << "  Actions: ";
        for(std::size_t i = 0; i < actions.size(); i++) {
            if(i > 0) cout << ", ";
            cout << actions[i].toString();
        }
        cout << endl;

        for(auto& child : action_node->getChildrens()) {
            printNodeIds(child, depth + 1);
        }
    } else if(node->getType() == GameTreeNode::CHANCE) {
        shared_ptr<ChanceNode> chance_node = std::dynamic_pointer_cast<ChanceNode>(node);
        cout << prefix << "[CHANCE] " << node->getNodeId() << endl;
        printNodeIds(chance_node->getChildren(), depth + 1);
    } else if(node->getType() == GameTreeNode::SHOWDOWN) {
        cout << prefix << "[SHOWDOWN] " << node->getNodeId() << endl;
    } else if(node->getType() == GameTreeNode::TERMINAL) {
        cout << prefix << "[TERMINAL] " << node->getNodeId() << endl;
    }
}

void CommandLineTool::dumpNodeStrategy(const string& node_id, const string& output_file) {
    shared_ptr<GameTreeNode> node = this->ps.get_game_tree()->findNodeById(node_id);
    if(!node) {
        cout << "Error: node not found: " << node_id << endl;
        return;
    }
    if(node->getType() != GameTreeNode::ACTION) {
        cout << "Error: node is not an action node: " << node_id << endl;
        return;
    }

    shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(node);

    // We need to get the trainable. Use deal=0 for now.
    shared_ptr<Trainable> trainable = action_node->getTrainable(0, false);
    if(!trainable) {
        cout << "Error: trainable not initialized. Run start_solve first, or build_tree first." << endl;
        return;
    }

    const vector<float>& avg_strategy = trainable->getAverageStrategy();
    vector<GameActions>& actions = action_node->getActions();
    vector<PrivateCards>* private_cards = action_node->player_privates;

    if(!private_cards) {
        cout << "Error: player_privates not set. Run start_solve first." << endl;
        return;
    }

    int card_number = private_cards->size();
    int action_number = actions.size();

    // Write CSV
    ofstream ofs(output_file);
    if(!ofs.is_open()) {
        cout << "Error: cannot open file for writing: " << output_file << endl;
        return;
    }

    // Header
    ofs << "hand";
    for(int a = 0; a < action_number; a++) {
        ofs << "," << actions[a].toString();
    }
    ofs << endl;

    // Rows
    for(int c = 0; c < card_number; c++) {
        ofs << (*private_cards)[c].toString();
        for(int a = 0; a < action_number; a++) {
            int index = a * card_number + c;
            ofs << "," << avg_strategy[index];
        }
        ofs << endl;
    }

    ofs.close();
    cout << "Dumped strategy for " << node_id << " to " << output_file
         << " (" << card_number << " hands, " << action_number << " actions)" << endl;
}

void CommandLineTool::loadNodeLock(const string& node_id, const string& input_file) {
    shared_ptr<GameTreeNode> node = this->ps.get_game_tree()->findNodeById(node_id);
    if(!node) {
        cout << "Error: node not found: " << node_id << endl;
        return;
    }
    if(node->getType() != GameTreeNode::ACTION) {
        cout << "Error: node is not an action node: " << node_id << endl;
        return;
    }

    shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(node);
    shared_ptr<Trainable> trainable = action_node->getTrainable(0, false);
    if(!trainable) {
        cout << "Error: trainable not initialized. Run start_solve first." << endl;
        return;
    }

    vector<PrivateCards>* private_cards = action_node->player_privates;
    if(!private_cards) {
        cout << "Error: player_privates not set." << endl;
        return;
    }

    int card_number = private_cards->size();
    vector<GameActions>& actions = action_node->getActions();
    int action_number = actions.size();

    // Read CSV
    ifstream ifs(input_file);
    if(!ifs.is_open()) {
        cout << "Error: cannot open file: " << input_file << endl;
        return;
    }

    // Skip header
    string header_line;
    getline(ifs, header_line);

    // Build a map from hand string to index
    map<string, int> hand_to_index;
    for(int c = 0; c < card_number; c++) {
        hand_to_index[(*private_cards)[c].toString()] = c;
    }

    // Initialize locked strategy with uniform distribution
    vector<float> locked_strategy(action_number * card_number, 1.0f / action_number);

    string line;
    int loaded_count = 0;
    while(getline(ifs, line)) {
        if(line.empty()) continue;

        // Parse CSV line: hand,prob1,prob2,...
        vector<string> tokens;
        stringstream ss(line);
        string token;
        while(getline(ss, token, ',')) {
            tokens.push_back(token);
        }

        if(tokens.size() < 2) continue;

        string hand_str = tokens[0];
        auto it = hand_to_index.find(hand_str);
        if(it == hand_to_index.end()) {
            cout << "Warning: hand " << hand_str << " not found in range, skipping" << endl;
            continue;
        }

        int card_idx = it->second;
        for(int a = 0; a < action_number && a + 1 < (int)tokens.size(); a++) {
            int index = a * card_number + card_idx;
            locked_strategy[index] = stof(tokens[a + 1]);
        }
        loaded_count++;
    }
    ifs.close();

    // Apply the lock to all deals (trainables)
    // For now, lock deal 0. In the future we could iterate all deals.
    vector<bool> locked_mask(card_number, true);
    trainable->lockStrategy(locked_strategy, locked_mask);

    cout << "Locked node " << node_id << " with " << loaded_count << " hands from " << input_file << endl;
    cout << "  (locked=" << trainable->isLocked() << ")" << endl;
}

void CommandLineTool::clearNodeLock(const string& node_id) {
    shared_ptr<GameTreeNode> node = this->ps.get_game_tree()->findNodeById(node_id);
    if(!node) {
        cout << "Error: node not found: " << node_id << endl;
        return;
    }
    if(node->getType() != GameTreeNode::ACTION) {
        cout << "Error: node is not an action node: " << node_id << endl;
        return;
    }

    shared_ptr<ActionNode> action_node = std::dynamic_pointer_cast<ActionNode>(node);
    shared_ptr<Trainable> trainable = action_node->getTrainable(0, false);
    if(!trainable) {
        cout << "Error: trainable not initialized." << endl;
        return;
    }

    trainable->unlockStrategy();
    cout << "Cleared lock on node " << node_id << endl;
}
