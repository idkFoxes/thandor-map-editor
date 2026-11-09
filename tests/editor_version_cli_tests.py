#!/usr/bin/env python3
"""Execute the actual main.cpp --version path with unresolved GUI calls.

Linux-only lazy linking keeps this independent of display/GPU SDKs. Reaching a
GUI entry point fails symbol resolution; this is not a Vulkan link/run test.
"""
import argparse,os,subprocess,tempfile
from pathlib import Path

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--compiler',default='g++');args=parser.parse_args();source=args.source.resolve()
    with tempfile.TemporaryDirectory(prefix='thandor-version-cli-')as directory:
        out=Path(directory)
        (out/'boundary.hpp').write_text('''
#include <GLFW/glfw3.h>
#include <imgui.h>
#define GLFW_CLIENT_API 1
#define GLFW_NO_API 2
#define GLFW_RESIZABLE 3
struct GLFWmonitor;
int glfwInit();
void glfwTerminate();
void glfwWindowHint(int,int);
GLFWwindow* glfwCreateWindow(int,int,const char*,GLFWmonitor*,GLFWwindow*);
void glfwDestroyWindow(GLFWwindow*);
namespace ImGui { void CheckVersionBoundary(); void CreateContext(); void DestroyContext(); }
#define IMGUI_CHECKVERSION() ImGui::CheckVersionBoundary()
''')
        (out/'driver.cpp').write_text('''
#include "thandor/editor_version.hpp"
#include <dlfcn.h>
#include <iostream>
#include <string>
int main(int argc,char**argv){
 if(!std::string(thandor::kEditorBuildLabel).ends_with(thandor::kEditorVersion)||
    !std::string(thandor::kEditorWindowTitle).ends_with(thandor::kEditorVersion))return 3;
 if(std::string(thandor::kEditorVersion)!=std::to_string(thandor::kEditorVersionMajor)+"."+
    std::to_string(thandor::kEditorVersionMinor)+"."+std::to_string(thandor::kEditorVersionPatch))return 4;
 auto lib=dlopen(argv[1],RTLD_LAZY|RTLD_LOCAL);
 if(!lib){std::cerr<<dlerror();return 1;}
 auto entry=reinterpret_cast<int(*)(int,char**)>(dlsym(lib,"main"));if(!entry)return 2;
 char name[]="editor",version[]="--version";char* args[]={name,version,nullptr};
 return entry(2,args);
}
''')
        flags=[args.compiler,'-std=c++20','-O0','-DGLFW_INCLUDE_NONE',
            '-I'+str(source/'tests/editor_gui_compile_stub'),'-I'+str(source/'include'),'-I'+str(source/'src/editor')]
        for command in [flags+['-shared','-fPIC','-include',str(out/'boundary.hpp'),str(source/'src/editor/main.cpp'),'-o',str(out/'main.so')],
                        flags+[str(out/'driver.cpp'),'-ldl','-o',str(out/'driver')]]:
            q=subprocess.run(command,capture_output=True,text=True)
            if q.returncode:raise RuntimeError(q.stdout+q.stderr)
        environment=dict(os.environ);environment.pop('DISPLAY',None);environment.pop('WAYLAND_DISPLAY',None)
        q=subprocess.run([str(out/'driver'),str(out/'main.so')],capture_output=True,text=True,env=environment)
        assert q.returncode==0,(q.returncode,q.stdout,q.stderr)
        import re
        assert re.fullmatch(r'Thandor Map Editor \d+\.\d+\.\d+\n',q.stdout),q.stdout
        print('EDITOR_VERSION_CLI PASS actual_main=1 GUI_calls_unresolved=1 '+q.stdout.strip())
if __name__=='__main__':main()
