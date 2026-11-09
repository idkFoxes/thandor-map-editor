#!/usr/bin/env python3
from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/core/simulation_lab_environment.cpp').read_text()
assert 'simulation_lab_live_target_aim_node_index(entity,nodes)' in s
assert 'simulation_lab_live_weapon_pose_node(entity,hierarchy,profile.aim_model_node_index,transforms.size())' in s
assert 'simulation_lab_live_weapon_pose_node(entity,hierarchy,profile.yaw_model_node_index,transforms.size())' in s
print('COMBAT_LIVE_AIM_ATTACHMENT_SOURCE_CONTRACT PASS')
