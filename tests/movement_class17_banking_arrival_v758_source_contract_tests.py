#!/usr/bin/env python3
from pathlib import Path
from source_contract_support import require_editor_version_at_least
import sys

root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
simulation=(root/'src/core/simulation.cpp').read_text()
lab=(root/'src/core/simulation_lab.cpp').read_text()
header=(root/'include/thandor/simulation.hpp').read_text()
cmake=(root/'CMakeLists.txt').read_text()

def require(value,message):
    if not value:
        raise SystemExit('FAIL: '+message)

require('simulation_tick_actor_motion_impl' in simulation,
        'shared movement callback helper is missing')
require('bool consume_at_half_distance' in simulation,
        'class-specific half-distance policy is not explicit')
require('callback_travel_q12' in simulation and '>>1' in simulation,
        'half-distance comparison is missing from the callback')
require('simulation_tick_class17_actor_motion' in simulation and
        'simulation_tick_class17_actor_motion' in header,
        'class-17 banking movement entry point is missing')
require('simulation_tick_actor_motion_impl(actor,motion,step_ticks,' in simulation and
        'motion.ground_speed_control' in simulation,
        'generic movement no longer limits half-distance consumption to ground callbacks')
require('entity.runtime_model_class_id == 17U' in lab and
        'simulation_tick_class17_actor_motion(entity.actor,entity.motion' in lab,
        'Simulation Lab does not dispatch runtime class 17 through the banking callback')
require('movement_class17_banking_arrival_v758_tests' in cmake,
        'V758 runtime regression is not registered')
require_editor_version_at_least(root, 758)
print('V758_CLASS17_BANKING_ARRIVAL_SOURCE_CONTRACT PASS')
