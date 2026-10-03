#include "include/ui/treemodel.h"
#include <QColor>
#include <QFont>

TreeModel::TreeModel(QSolverJob * data, QObject *parent)
    : QAbstractItemModel(parent)
{
    this->qSolverJob = data;
    setupModelData();
}

TreeModel::~TreeModel()
{
    delete rootItem;
}

int TreeModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<TreeItem*>(parent.internalPointer())->columnCount();
    return rootItem->columnCount();
}

QVariant TreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    if (!item) return QVariant();

    // Helper lambda to check lock status
    auto isNodeLocked = [this](TreeItem* ti) -> bool {
        if (!ti) return false;
        shared_ptr<GameTreeNode> node = ti->m_treedata.lock();
        if (!node || node->getType() != GameTreeNode::GameTreeNodeType::ACTION) return false;

        std::string nodeId = node->getNodeId();
        for (const auto& kv : qSolverJob->locked_strategies) {
            const std::string& key = kv.first;
            size_t hash_pos = key.find('#');
            if (hash_pos != std::string::npos) {
                std::string id = key.substr(0, hash_pos);
                if (id == nodeId) return true;
            }
        }
        return false;
    };

    bool locked = isNodeLocked(item);

    if (role == Qt::DisplayRole) {
        QVariant base = item->data();
        if (locked) {
            return base.toString() + " 🔒";
        }
        return base;
    }

    if (role == Qt::ForegroundRole) {
        if (locked) {
            return QColor(220, 53, 69); // Sleek modern Crimson Red
        }
    }

    if (role == Qt::FontRole) {
        if (locked) {
            QFont font;
            font.setBold(true);
            return font;
        }
    }

    return QVariant();
}

Qt::ItemFlags TreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant TreeModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return rootItem->data();

    return QVariant();
}

QModelIndex TreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeItem *parentItem;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    TreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex TreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    TreeItem *childItem = static_cast<TreeItem*>(index.internalPointer());
    TreeItem *parentItem = childItem->parentItem();

    if (parentItem == rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int TreeModel::rowCount(const QModelIndex &parent) const
{
    TreeItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

bool TreeModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return true;

    TreeItem *parentItem = static_cast<TreeItem*>(parent.internalPointer());
    if (!parentItem) return false;

    if (parentItem->childCount() > 0)
        return true;

    shared_ptr<GameTreeNode> gameTreeNode = parentItem->m_treedata.lock();
    if (!gameTreeNode) return false;

    if (gameTreeNode->getType() == GameTreeNode::GameTreeNodeType::ACTION) {
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(gameTreeNode);
        return !actionNode->getChildrens().empty();
    }
    else if (gameTreeNode->getType() == GameTreeNode::GameTreeNodeType::CHANCE) {
        shared_ptr<ChanceNode> chanceNode = dynamic_pointer_cast<ChanceNode>(gameTreeNode);
        return chanceNode->getChildren() != nullptr;
    }

    return false;
}

void TreeModel::reGenerateTreeItem(GameTreeNode::GameRound round,TreeItem* node_to_process){
    if (node_to_process->childCount() > 0) return;

    const shared_ptr<GameTreeNode> gameTreeNode = node_to_process->m_treedata.lock();
    if (!gameTreeNode) return;

    QModelIndex parentIndex;
    if (node_to_process != rootItem) {
        parentIndex = createIndex(node_to_process->row(), 0, node_to_process);
    }

    if(gameTreeNode->getType() == GameTreeNode::GameTreeNodeType::ACTION){
        shared_ptr<ActionNode> actionNode = dynamic_pointer_cast<ActionNode>(gameTreeNode);
        vector<shared_ptr<GameTreeNode>>& childrens = actionNode->getChildrens();
        
        int insert_end = childrens.size() - 1;
        if (insert_end >= 0) {
            beginInsertRows(parentIndex, 0, insert_end);
            for(shared_ptr<GameTreeNode> one_child:childrens){
                TreeItem * child_node = new TreeItem(one_child,node_to_process);
                node_to_process->insertChild(child_node);
            }
            endInsertRows();
        }

        for (int i = 0; i < node_to_process->childCount(); ++i) {
            TreeItem* child_node = node_to_process->child(i);
            if (child_node->m_treedata.lock()->getRound() == round) {
                this->reGenerateTreeItem(round, child_node);
            }
        }
    }
    else if(gameTreeNode->getType() == GameTreeNode::GameTreeNodeType::CHANCE){
        shared_ptr<ChanceNode> chanceNode = dynamic_pointer_cast<ChanceNode>(gameTreeNode);
        shared_ptr<GameTreeNode> child = chanceNode->getChildren();
        if (child) {
            beginInsertRows(parentIndex, 0, 0);
            TreeItem * child_node = new TreeItem(child,node_to_process);
            node_to_process->insertChild(child_node);
            endInsertRows();
            
            this->reGenerateTreeItem(round,child_node);
        }
    }
}

void TreeModel::setupModelData()
{
    PokerSolver * solver;
    if(this->qSolverJob->mode == QSolverJob::Mode::HOLDEM){
        solver = &(this->qSolverJob->ps_holdem);
    }else if(this->qSolverJob->mode == QSolverJob::Mode::SHORTDECK){
        solver = &(this->qSolverJob->ps_shortdeck);
    }else{
        throw runtime_error("holdem mode incorrect");
    }

    if(solver->get_game_tree() == nullptr || solver->get_game_tree()->getRoot() == nullptr){
        return;
    }

    GameTreeNode::GameRound round =	solver->get_game_tree()->getRoot()->getRound();

    this->rootItem = new TreeItem(solver->get_game_tree()->getRoot());
    TreeItem* ti = new TreeItem(solver->get_game_tree()->getRoot(),this->rootItem);
    rootItem->insertChild(ti);
    this->reGenerateTreeItem(round,ti);
}


void TreeModel::clicked_event(const QModelIndex & index){
}
