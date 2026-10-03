import torch
import sys
import os

# Add scripts dir to path so we can import value_network
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from value_network import PokerValueNetwork

def export():
    model = PokerValueNetwork(
        num_clusters=100,
        num_positions=20,
        num_scenarios=100,
        num_stack_clusters=20
    )
    
    pth_path = os.path.join(os.path.dirname(__file__), '..', 'value_network.pth')
    if not os.path.exists(pth_path):
        print(f"ERROR: {pth_path} not found. Train the model first.")
        return
    
    model.load_state_dict(torch.load(pth_path, weights_only=True, map_location='cpu'))
    model.eval()
    
    # Create example inputs for tracing
    batch_size = 1
    board = torch.zeros(batch_size, 52)
    pot_stack_spr = torch.zeros(batch_size, 3)
    oop_range = torch.zeros(batch_size, 1326)
    ip_range = torch.zeros(batch_size, 1326)
    oop_cluster_id = torch.zeros(batch_size, dtype=torch.long)
    ip_cluster_id = torch.zeros(batch_size, dtype=torch.long)
    oop_pos_id = torch.zeros(batch_size, dtype=torch.long)
    ip_pos_id = torch.zeros(batch_size, dtype=torch.long)
    scenario_id = torch.zeros(batch_size, dtype=torch.long)
    stack_cluster_id = torch.zeros(batch_size, dtype=torch.long)
    
    traced = torch.jit.trace(model, (
        board, pot_stack_spr, oop_range, ip_range,
        oop_cluster_id, ip_cluster_id, oop_pos_id, ip_pos_id,
        scenario_id, stack_cluster_id
    ))
    
    out_path = os.path.join(os.path.dirname(__file__), '..', 'value_network_traced.pt')
    traced.save(out_path)
    print(f"Exported TorchScript model to {out_path}")
    
    # Validate
    with torch.no_grad():
        orig_oop, orig_ip = model(board, pot_stack_spr, oop_range, ip_range,
                                   oop_cluster_id, ip_cluster_id, oop_pos_id, ip_pos_id,
                                   scenario_id, stack_cluster_id)
        traced_oop, traced_ip = traced(board, pot_stack_spr, oop_range, ip_range,
                                        oop_cluster_id, ip_cluster_id, oop_pos_id, ip_pos_id,
                                        scenario_id, stack_cluster_id)
    
    diff_oop = (orig_oop - traced_oop).abs().max().item()
    diff_ip = (orig_ip - traced_ip).abs().max().item()
    print(f"Validation — max OOP diff: {diff_oop:.8f}, max IP diff: {diff_ip:.8f}")
    if diff_oop < 1e-5 and diff_ip < 1e-5:
        print("PASS: Exported model matches original.")
    else:
        print("WARNING: Outputs differ beyond tolerance!")

if __name__ == "__main__":
    export()
