#!/usr/bin/env python3
"""Exercise build entry points with a deterministic fake compiler/packager.
No GUI SDK, network access, or cross compiler is needed for this control-flow test.
"""
import json, os, pty, shutil, subprocess, sys, tempfile
from pathlib import Path
source=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='thandor-build-test-') as tmp:
    root=Path(tmp)/'source with spaces'; root.mkdir(); (root/'tools').mkdir()
    for name in ('build_linux.sh','build_linux_andassets.sh','build_windows_from_linux.sh','tools/build_common.sh','tools/clean_build_directory_preserving_artifacts.sh'):
        shutil.copy2(source/name,root/name)
    fake=Path(tmp)/'fake';fake.mkdir();calls=Path(tmp)/'calls.jsonl'
    cmake=fake/'cmake'
    cmake.write_text('''#!/usr/bin/env python3
import sys,json,os
from pathlib import Path
args=sys.argv[1:]
with open(os.environ['CALLS'],'a')as f:f.write(json.dumps(['cmake']+args)+'\\n')
if os.environ.get('FAIL_CONFIGURE')=='1' and '-S' in args:sys.exit(23)
if '-S' in args:
 p=Path(args[args.index('-B')+1]);p.mkdir(parents=True,exist_ok=True);(p/'CMakeCache.txt').touch()
if '--build' in args:
 p=Path(args[1]);(p/'thandor-map-editor').write_text('fake');(p/'thandor-map-editor').chmod(0o755)
 (p/'thandor-map-editor.exe').write_text('fake');(p/'shaders').mkdir(exist_ok=True);(p/'shaders/main.spv').write_bytes(b'fake')
 (p/'assets/fonts').mkdir(parents=True,exist_ok=True);(p/'assets/fonts/DejaVuSans.ttf').write_bytes(b'font');(p/'assets/fonts/LICENSE').write_text('font license')
if args[:2]==['-E','tar']:Path(args[3]).touch()
''');cmake.chmod(0o755)
    ctest=fake/'ctest';ctest.write_text('''#!/usr/bin/env python3
import sys,json,os
with open(os.environ['CALLS'],'a')as f:f.write(json.dumps(['ctest']+sys.argv[1:])+'\\n')
''');ctest.chmod(0o755)
    build=Path(tmp)/'cached build';build.mkdir();(build/'CMakeCache.txt').touch();(build/'keep.object').touch();(build/'DATA.PCK').write_text('asset')
    env={**os.environ,'CALLS':str(calls),'THANDOR_CMAKE_COMMAND':str(cmake),'THANDOR_BUILD_DIR':str(build),'THANDOR_BUILD_JOBS':'2','THANDOR_TEST_JOBS':'3','THANDOR_BUILD_APP':'0','THANDOR_CLEAN_BUILD':'0'}
    def run(script,updates=None,expected=0):
        calls.write_text('');p=subprocess.run(['bash',str(root/script)],env={**env,**(updates or {})},text=True,capture_output=True,stdin=subprocess.DEVNULL)
        assert p.returncode==expected,(script,p.returncode,p.stdout,p.stderr)
        return [json.loads(x) for x in calls.read_text().splitlines()]
    rows=run('build_linux.sh',{'THANDOR_TEST_LABEL':'research','THANDOR_TEST_REGEX':'catalog'})
    assert (build/'keep.object').exists(),'incremental build deleted object'
    assert '-G' not in rows[0],'cached generator changed'
    assert rows[1][-2:]==['--parallel','2']
    assert rows[2][-4:]==['-L','research','-R','catalog']
    assert rows[2][rows[2].index('--parallel')+1]=='3'
    rows=run('build_linux.sh',{'THANDOR_CLEAN_BUILD':'1','THANDOR_GENERATOR':'Unix Makefiles'})
    assert not (build/'keep.object').exists() and (build/'DATA.PCK').read_text()=='asset'
    assert rows[0][rows[0].index('-G')+1]=='Unix Makefiles'
    rows=run('build_linux.sh',{'THANDOR_BUILD_TESTS':'0'})
    assert all(x[0]!='ctest' for x in rows)
    rows=run('build_linux.sh',{'THANDOR_RUN_TESTS':'0'})
    assert all(x[0]!='ctest' for x in rows)
    rows=run('build_linux_andassets.sh')
    assert '-DCMAKE_BUILD_TYPE=Release' in rows[0] and '-DTHANDOR_EDITOR_BUILD_PROBES=1' in rows[0]
    run('build_linux.sh',{'FAIL_CONFIGURE':'1'},23)
    run('build_linux.sh',{'THANDOR_BUILD_JOBS':'0'},64)
    # Prompt only after a successful GUI compilation. No/Enter still run tests,
    # and CI can explicitly select yes/no without an interactive terminal.
    (root/'tools/build_appimage.sh').write_text('''#!/usr/bin/env bash
printf '["appimage"]\\n' >> "$CALLS"
exit "${FAIL_APPIMAGE:-0}"
''')
    def prompted(answer, expected=0, updates=None):
        calls.write_text('');master,slave=pty.openpty()
        try:
            proc=subprocess.Popen(['bash',str(root/'build_linux.sh')],
                env={**env,'THANDOR_BUILD_APP':'1','THANDOR_BUILD_APPIMAGE':'ask',**(updates or {})},
                stdin=slave,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            os.close(slave);slave=None
            os.write(master,answer.encode())
            stdout,stderr=proc.communicate(timeout=10)
            assert proc.returncode==expected,(stdout,stderr)
            assert 'Compilation finished. Create an AppImage? [y/N]' in stderr
            return [json.loads(x) for x in calls.read_text().splitlines()]
        finally:
            if slave is not None:os.close(slave)
            os.close(master)
    for answer in ('no\n','\n'):
        rows=prompted(answer)
        assert all(x[0]!='appimage' for x in rows) and rows[-1][0]=='ctest'
    for answer in ('yes\n','invalid\ny\n'):
        rows=prompted(answer)
        assert [x[0] for x in rows]==['cmake','cmake','appimage','ctest']
    rows=prompted('yes\n',31,{'FAIL_APPIMAGE':'31'})
    assert rows[-1][0]=='appimage','packaging failure was hidden'
    rows=run('build_linux.sh',{'THANDOR_BUILD_APP':'1','THANDOR_BUILD_APPIMAGE':'yes'})
    assert [x[0] for x in rows]==['cmake','cmake','appimage','ctest']
    rows=run('build_linux.sh',{'THANDOR_BUILD_APP':'1','THANDOR_BUILD_APPIMAGE':'no'})
    assert all(x[0]!='appimage' for x in rows)
    rows=run('build_linux.sh',{'THANDOR_BUILD_APP':'1','THANDOR_BUILD_APPIMAGE':'ask'})
    assert all(x[0]!='appimage' for x in rows),'noninteractive build attempted to prompt/package'
    # Refuse destructive cleans of the source and ancestors before touching data.
    for bad in (root,root.parent,Path('/')):
        p=subprocess.run(['bash',str(root/'tools/clean_build_directory_preserving_artifacts.sh'),str(bad)],capture_output=True)
        assert p.returncode==64 and (root/'build_linux.sh').exists()
    deps=Path(tmp)/'windows deps';deps.mkdir()
    paths={
        'TOOLCHAIN':deps/'toolchain.cmake','VULKAN_INCLUDE':deps/'include',
        'VULKAN_LIBRARY':deps/'vulkan.a','GLSLC':deps/'glslc',
        'GLFW_SOURCE':deps/'glfw','IMGUI_SOURCE':deps/'imgui'}
    for key,path in paths.items():
        file=path/({'VULKAN_INCLUDE':'vulkan/vulkan.h','GLFW_SOURCE':'CMakeLists.txt','IMGUI_SOURCE':'imgui.h'}[key]) if key in ('VULKAN_INCLUDE','GLFW_SOURCE','IMGUI_SOURCE') else path
        file.parent.mkdir(parents=True,exist_ok=True);file.touch()
    import shlex
    (deps/'prepared.env').write_text(''.join('THANDOR_WINDOWS_'+k+'='+shlex.quote(str(v))+'\n' for k,v in paths.items())+'THANDOR_WINDOWS_TRIPLET=test-missing-triplet\n')
    dist=root/'dist/windows-x64';dist.mkdir(parents=True);(dist/'keep.PCK').write_text('package asset')
    rows=run('build_windows_from_linux.sh',{'THANDOR_WINDOWS_DEPS_DIR':str(deps),'THANDOR_WINDOWS_BUILD_DIR':str(build),'THANDOR_WINDOWS_DIST_DIR':str(dist),'THANDOR_WINE_SMOKE_TEST':'0'})
    assert '-G' not in rows[0] and rows[1][-2:]==['--parallel','2']
    assert (dist/'keep.PCK').read_text()=='package asset'
    assert (dist/'thandor-map-editor.exe').exists() and (dist/'shaders/main.spv').exists()
    assert (dist/'assets/fonts/DejaVuSans.ttf').read_bytes()==b'font'
    assert (dist/'assets/fonts/LICENSE').read_text()=='font license'
    assert 'assets/fonts/DejaVuSans.ttf' in (dist/'SHA256SUMS.txt').read_text()
    assert (root/'dist/ThandorMapEditor-windows-x64.zip').exists()
    assert all(x[0]!='ctest' for x in rows),'cross compiled tests require an explicit emulator'
print('PASS: incremental/clean, preserved assets, cached/fresh generators, jobs, filters, failure propagation, Linux AppImage prompt/overrides, Windows package flow')
