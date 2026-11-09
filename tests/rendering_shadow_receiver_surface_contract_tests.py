#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
app=(root/'src/editor/editor_app.cpp').read_text()
guard=(root/'src/editor/shadow_receiver_surface_guard.hpp').read_text()
assert '#include "shadow_receiver_surface_guard.hpp"' in app
assert 'shadow_receiver_triangle_has_unresolved_contact(a.position,b.position,c.position' in app
assert 'shadow_receiver_triangle_is_upright((*projected)[ia].position' in app
assert 'if(!sample.inside) return std::nullopt;' in app
assert 'std::span<const uint32_t> indices;' in app
assert 'packet.indices=std::span<const uint32_t>(indices).subspan(' in app
assert 'std::stable_sort(deferred.begin(),deferred.end()' in app
assert 'return left.source_order<right.source_order;' in app
assert 'maximum_contact_error=0.18F' in guard
print('rendering shadow surface and allocation-free sort source contract PASS')
