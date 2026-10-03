#ifndef TEXASSOLVER_PREFLOPGAMETREENODE_H
#define TEXASSOLVER_PREFLOPGAMETREENODE_H

#include <vector>
#include <string>
#include <memory>
#include <map>
#include "include/preflop/PreflopRule.h"
#include "include/trainable/Trainable.h"

using namespace std;

class PreflopGameTreeNode : public enable_shared_from_this<PreflopGameTreeNode> {
public:
    enum NodeType { ACTION, SHOWDOWN, TERMINAL };
    
protected:
    NodeType type;
    shared_ptr<PreflopGameTreeNode> parent;
    PreflopRule rule;
    string path_action; // Action name that led to this node (e.g., "CALL", "RAISE 200")

public:
    PreflopGameTreeNode(NodeType type, shared_ptr<PreflopGameTreeNode> parent, const PreflopRule& rule, string path_action = "")
        : type(type), parent(parent), rule(rule), path_action(path_action) {}
        
    virtual ~PreflopGameTreeNode() = default;

    NodeType getType() const { return type; }
    shared_ptr<PreflopGameTreeNode> getParent() const { return parent; }
    const PreflopRule& getRule() const { return rule; }
    string getPathAction() const { return path_action; }
};

class PreflopActionNode : public PreflopGameTreeNode {
private:
    int player; // Player ID acting at this node
    vector<string> actions; // Possible actions at this node
    vector<shared_ptr<PreflopGameTreeNode>> children;
    shared_ptr<Trainable> trainable; // CFR trainable regrets/strategy

public:
    PreflopActionNode(shared_ptr<PreflopGameTreeNode> parent, const PreflopRule& rule, int player, string path_action = "")
        : PreflopGameTreeNode(NodeType::ACTION, parent, rule, path_action), player(player), trainable(nullptr) {}

    int getPlayer() const { return player; }
    const vector<string>& getActions() const { return actions; }
    const vector<shared_ptr<PreflopGameTreeNode>>& getChildren() const { return children; }
    
    void add_child(const string& action, shared_ptr<PreflopGameTreeNode> child) {
        actions.push_back(action);
        children.push_back(child);
    }
    
    shared_ptr<Trainable> getTrainable() { return trainable; }
    void setTrainable(shared_ptr<Trainable> t) { trainable = t; }
};

class PreflopShowdownNode : public PreflopGameTreeNode {
#ifdef USE_LIBTORCH
public:
    bool has_nn_evs = false;
    std::vector<float> nn_oop_evs;
    std::vector<float> nn_ip_evs;
#endif

public:
    PreflopShowdownNode(shared_ptr<PreflopGameTreeNode> parent, const PreflopRule& rule, string path_action = "")
        : PreflopGameTreeNode(NodeType::SHOWDOWN, parent, rule, path_action) {}
};

class PreflopTerminalNode : public PreflopGameTreeNode {
private:
    int winner; // Winner ID

public:
    PreflopTerminalNode(shared_ptr<PreflopGameTreeNode> parent, const PreflopRule& rule, int winner, string path_action = "")
        : PreflopGameTreeNode(NodeType::TERMINAL, parent, rule, path_action), winner(winner) {}

    int getWinner() const { return winner; }
};

#endif // TEXASSOLVER_PREFLOPGAMETREENODE_H
