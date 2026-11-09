#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,required=True)
args=parser.parse_args()
root=args.source
cpp=(root/'src/editor/editor_app.cpp').read_text(encoding='utf-8')
hpp=(root/'src/editor/editor_app.hpp').read_text(encoding='utf-8')
core=(root/'src/core/frontend_player_texture.cpp').read_text(encoding='utf-8')
cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8')

def need(text,needle,message):
    if needle not in text:
        raise SystemExit('FAIL: '+message)

need(cpp,'find_loose_file_case_insensitive(catalog_.game_root(),"thandor.dat")',
     'editor no longer reads the retail persistent settings image')
need(cpp,'persistent_settings_player_name',
     'editor no longer recovers the persisted frontend player name')
need(cpp,'retail_frontend_player_pcx_leaf',
     'editor no longer uses retail player-PCX filename sanitization')
need(cpp,'kRetailFrontendPlayerTextureSubresource=113U',
     'retail army graphics subresource 113 override was lost')
need(cpp,'runtime_faction==local_player_runtime_faction_',
     'player PCX is no longer binding/faction-specific')
need(cpp,'army_texture_layer_key(runtime_faction,graphics_family,subresource',
     'dynamic object layer lookup no longer distinguishes runtime binding from graphics family')
need(cpp,'Local player PCX faction',
     'standalone editor no longer exposes the frontend faction assignment needed outside a live lobby')
need(hpp,'local_player_texture_override_',
     'editor no longer retains decoded local player texture override')
need(core,'constexpr std::size_t kPlayerNameOffset = 0x60U;',
     'thandor.dat player-name offset changed')
need(core,"case u'*': case u'.': case u'?': case u'/': case u'\\\\':",
     'retail frontend filename forbidden-character filter changed')
need(core,'black ? 0U : 255U',
     'retail PCX palette alpha rule changed; RGB black must be transparent')
need(cmake,'src/core/frontend_player_texture.cpp',
     'frontend player texture decoder is not linked into core')
need(cmake,'rendering_frontend_player_texture_tests',
     'frontend player texture binary regression is not registered')
print('v555 frontend player color source contract passed')
