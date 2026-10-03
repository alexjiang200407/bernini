import subprocess, json, sys, os
import util.cmake_tools as ct, util.config as cfg, shlex
preset="windows-ninja-msvc-dx12-debug"
env,_=cfg.build_env(ct.generator_of(preset))
ninja=cfg.find_ninja(env); bd=ct.binary_dir_of(preset)
db=json.loads(subprocess.run([ninja,"-C",bd,"-t","compdb"],env=env,capture_output=True,text=True).stdout)
e=[x for x in db if x["file"].endswith("err\\util.cpp") or x["file"].endswith("err/util.cpp")][0]
cmd=e["command"]
print(cmd[:3000])
import re
cmd=cmd.split(" ",1)[1] if ".bat" in cmd.split(" ",1)[0] else cmd
cmd=re.sub(r' /Fo\S+',' ',cmd); cmd=re.sub(r' /Fd\S+',' ',cmd); cmd=cmd.replace(" /showIncludes","").replace(" /FS","")
cmd=cmd.replace(" -c "," /E ")
r=subprocess.run(cmd,shell=True,cwd=e["directory"],env=env,capture_output=True,text=True)
print("exit",r.returncode,"stdout bytes",len(r.stdout))
print("\n".join(r.stderr.splitlines()[:25]))
print("\n".join(r.stdout.splitlines()[:5]))
