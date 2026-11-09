#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
core = (root / 'src/core/simulation_lab.cpp').read_text()
header = (root / 'include/thandor/simulation_lab.hpp').read_text()
cmake = (root / 'CMakeLists.txt').read_text()

failures = []
def req(condition, message):
    if not condition:
        failures.append(message)

req('producer_available_for_purchase' in header,
    'eligible-producer helper is not part of the Simulation Lab contract')
req('AiPurchaseCandidate_HasEligibleProducerCf (0x00537630)' in core,
    'machine-code source for producer admission is no longer documented')
choose_start = core.index('SimulationLabExperiment::choose_construction_asset')
choose_end = core.index('SimulationLabExperiment::construction_position', choose_start)
choose = core[choose_start:choose_end]
# Retail collects weights before producer admission. Admission belongs to
# affordable execution before secondary registration, not candidate scoring.
req('producer_available_for_purchase(producer, *candidate, faction)' not in choose,
    'candidate collection prematurely filters busy producers before weighted execution')
plan_start = core.index('void SimulationLabExperiment::plan_construction')
plan_end = core.index('void SimulationLabExperiment::complete_construction', plan_start)
plan = core[plan_start:plan_end]
req('producer_available_for_purchase(producer, *army)' in plan,
    'affordable execution lost the eligible-live-producer admission gate')
req('run.secondary_purchase_requests.push_back(request);' in plan,
    'secondary purchase registration disappeared')
start_prod = core[core.index('void SimulationLabExperiment::start_production_jobs'):
                  core.index('namespace {', core.index('void SimulationLabExperiment::start_production_jobs'))]
req('producer_available_for_purchase(producer, *candidate)' in start_prod,
    'producer dequeue no longer shares the retail eligibility gate')
req('production_bootstrap_production_tests' in cmake,
    'four-faction V568 bootstrap regression is not registered')

if failures:
    for failure in failures:
        print('FAIL:', failure)
    raise SystemExit(1)
print('V568_BOOTSTRAP_PRODUCER_ELIGIBILITY_SOURCE_CONTRACT PASS')
