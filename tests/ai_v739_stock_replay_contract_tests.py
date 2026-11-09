from pathlib import Path
root=Path(__file__).resolve().parent.parent
code=(root/'tools/simlab_stock_replay.cpp').read_text()
assert 'military_ai_unseen_fallback_omitted' not in code
assert 'military_ai_unseen_group_targets_bounded' in code
assert 'military_ai_unseen_group_targets_unavailable' in code
script=(root/'tools/simlab_stock_map_matrix.py').read_text()
for check in ('subprocess.run(', 'statistics.median', 'capture_output=True', 'timeout=args.timeout','RUN index='):
    assert check in script,check
assert 'V739' in (root/'include/thandor/editor_version.hpp').read_text()
print('V739 stock replay contract PASS')
