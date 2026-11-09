#!/usr/bin/env python3
"""Build the bundled Simulation Lab replay executable from its complete editor source."""
import argparse,subprocess,os
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--editor',type=Path,default=Path(__file__).resolve().parent.parent/'editor/ThandorMapEditor_Vulkan_Source');p.add_argument('--build-dir',type=Path,default=Path('lab_build'));p.add_argument('--jobs',type=int,default=2);a=p.parse_args();e=a.editor.resolve();b=a.build_dir.resolve();r=Path(__file__).resolve().parent
subprocess.run(['cmake','-S',str(e),'-B',str(b),'-DTHANDOR_EDITOR_BUILD_APP=OFF','-DTHANDOR_EDITOR_BUILD_TESTS=OFF'],check=True)
subprocess.run(['cmake','--build',str(b),'--target','thandor_simlab_stock_replay','-j',str(a.jobs)],check=True)
import shutil
shutil.copy2(b/'thandor_simlab_stock_replay',r/'lab_replay')
