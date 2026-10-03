import subprocess, sys, os, re
import util.cmake_tools as ct, util.config as cfg
preset="windows-ninja-msvc-dx12-debug"
env,_=cfg.build_env(ct.generator_of(preset))
ninja=cfg.find_ninja(env); bd=ct.binary_dir_of(preset)
log=os.environ["CCACHE_LOGFILE"]
subprocess.run([ninja,"-C",bd,"libs\\core\\CMakeFiles\\core.dir\\src\\err\\util.cpp.obj"],env=env)
lines=open(log,errors="replace").read().splitlines()
for i,l in enumerate(lines):
    if "Running preprocessor" in l:
        ex=lines[i+1]; break
cmd=ex.split("Executing ",1)[1]
print("PREPROC:",cmd)
r=subprocess.run(cmd,shell=True,cwd=bd,env=env,capture_output=True,text=True)
print("exit",r.returncode,len(r.stdout))
print("STDERR:","\n".join(r.stderr.splitlines()[:30]))
print("STDOUT-HEAD:","\n".join(r.stdout.splitlines()[:5]))
