import torch
import torch.nn as nn

class PokerValueNetwork(nn.Module):
    def __init__(self, num_clusters=20, num_positions=10, num_scenarios=50, num_stack_clusters=10, emb_dim=16):
        super(PokerValueNetwork, self).__init__()
        
        # Embeddings for categorical features
        self.cluster_emb = nn.Embedding(num_clusters, emb_dim)
        self.position_emb = nn.Embedding(num_positions, emb_dim)
        self.scenario_emb = nn.Embedding(num_scenarios, emb_dim)
        self.stack_cluster_emb = nn.Embedding(num_stack_clusters, emb_dim)
        
        # Board embedding (5 cards max, 52 possible cards. We can use a simple 52-dim binary vector)
        # So board is just a 52-dim float vector of 0s and 1s
        board_dim = 52
        
        # Continuous features: pot, stack, spr
        cont_dim = 3
        
        # Range inputs: OOP and IP (1326 each)
        range_dim = 1326 * 2
        
        # Calculate total input dimension
        # OOP cluster, IP cluster, OOP pos, IP pos, Scenario, Stack Cluster = 6 embeddings
        total_emb_dim = emb_dim * 6
        
        self.input_dim = board_dim + cont_dim + range_dim + total_emb_dim
        
        # Shared MLP
        self.shared_mlp = nn.Sequential(
            nn.Linear(self.input_dim, 2048),
            nn.ReLU(),
            nn.BatchNorm1d(2048),
            nn.Dropout(0.2),
            
            nn.Linear(2048, 1024),
            nn.ReLU(),
            nn.BatchNorm1d(1024),
            nn.Dropout(0.2),
            
            nn.Linear(1024, 1024),
            nn.ReLU(),
            nn.BatchNorm1d(1024)
        )
        
        # Output heads for OOP and IP expected values (1326 each)
        self.oop_head = nn.Sequential(
            nn.Linear(1024, 1024),
            nn.ReLU(),
            nn.Linear(1024, 1326)
        )
        
        self.ip_head = nn.Sequential(
            nn.Linear(1024, 1024),
            nn.ReLU(),
            nn.Linear(1024, 1326)
        )

    def forward(self, board, pot_stack_spr, oop_range, ip_range, 
                oop_cluster_id, ip_cluster_id, oop_pos_id, ip_pos_id, 
                scenario_id, stack_cluster_id):
        
        # Embeddings
        oop_c_emb = self.cluster_emb(oop_cluster_id)
        ip_c_emb = self.cluster_emb(ip_cluster_id)
        oop_p_emb = self.position_emb(oop_pos_id)
        ip_p_emb = self.position_emb(ip_pos_id)
        scen_emb = self.scenario_emb(scenario_id)
        stack_c_emb = self.stack_cluster_emb(stack_cluster_id)
        
        # Concatenate all inputs
        x = torch.cat([
            board,              # [batch_size, 52]
            pot_stack_spr,      # [batch_size, 3]
            oop_range,          # [batch_size, 1326]
            ip_range,           # [batch_size, 1326]
            oop_c_emb,          # [batch_size, emb_dim]
            ip_c_emb,           # [batch_size, emb_dim]
            oop_p_emb,          # [batch_size, emb_dim]
            ip_p_emb,           # [batch_size, emb_dim]
            scen_emb,           # [batch_size, emb_dim]
            stack_c_emb         # [batch_size, emb_dim]
        ], dim=1)
        
        # Pass through shared MLP
        features = self.shared_mlp(x)
        
        # Get EVs
        ev_oop = self.oop_head(features)
        ev_ip = self.ip_head(features)
        
        return ev_oop, ev_ip
