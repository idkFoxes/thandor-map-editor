from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1])
app=(root/'src/editor/editor_app.cpp').read_text()
header=(root/'src/editor/editor_app.hpp').read_text()
model=(root/'src/core/model.cpp').read_text()
simulation=(root/'include/thandor/simulation.hpp').read_text()
pose=(root/'src/editor/articulated_walker_pose.hpp').read_text()

def need(condition,message):
    if not condition:
        raise SystemExit('FPS_WALKER_FOOTPRINT_SOURCE_CONTRACT FAIL: '+message)

need('measured_fps_label_' in app and 'show_measured_fps_counter_' in app and 'ImGui::SameLine(right_x)' in app and
     'ImGui::TextColored({0.50F,0.84F,0.69F,1.0F}' in app and
     'draw_measured_fps_overlay' not in app,
     'measured FPS is not a persistent right-aligned navbar label')
need('record_presented_frame(glfwGetTime())' in app,
     'FPS is not sampled from wall clock after the present boundary')
need(app.index('vulkan_.end_frame(*frame);') < app.index('record_presented_frame(glfwGetTime())'),
     'FPS counts frames before the end-frame/present boundary')
need('ImGui::GetIO().Framerate' not in app,
     'FPS uses ImGui smoothed UI rate instead of measured presented frames')
need('elapsed<0.5' in app and 'measured_fps_presented_frames_' in header,
     'FPS lacks an explicit presented-frame sampling window')
need('The first presented frame defines the window origin' in app and
     'measured_fps_presented_frames_=0U' in app,
     'FPS first sampling window still counts its origin frame and is biased high')
need('articulated_contact_steering_limit_turn16' in model and
     'read_u32(data, offset + 0xC8U)' in model,
     'class-3 MDL +0xC8 steering/contact clamp is not parsed')
need('articulated_foot_target_world' in simulation and 'articulated_pose_transition_active' in simulation,
     'frozen class-3 contact endpoint state missing')
need('actor,false' in app and 'commit_articulated_contact_state' in app,
     'render paths are still allowed to advance class-3 contact state')
need('4.0F*lift*progress*(1.0F-progress)' in pose,
     'V471 parabolic class-3 contact lift equation missing')
need('relative>=0x4000U&&relative<=0xC000U' in pose and 'planar<0x140' in pose and
     'planar<0x40' in pose,
     'V471 first/root leg-child planar articulation and distance falloff missing')
need('terrain_contact_root_transform' in pose and 'transforms[foot].basis=contact_orientation.basis' in pose,
     'terminal foot basis is still inherited from the lower leg instead of terrain-aligned')
need('articulated_walker_detail::leg_chain' in app and
     'articulated_contact_completed' in app and
     'MovementGroundEffectInstance{' in app and 'true});' in app,
     'terminal-foot contact effect path missing')
need('timed_water_effect_id:root->simulation.timed_effect_id' in app.replace('\n',''),
     'land/water footprint effect selection missing')
need('child down child-slot zero three times' in app,
     'footprint path lacks recovered terminal-child provenance')
print('FPS_WALKER_FOOTPRINT_SOURCE_CONTRACT PASS')
