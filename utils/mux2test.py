#!/usr/bin/env python
import getopt
import os
import re
import subprocess
import sys

optlist, args = getopt.gnu_getopt(sys.argv[1:], 'O:', [
    'allow-spurious-exports',
    'known-dynamic-defs',
    'known-dynamic-uses',
    'known-rtld-local',
    'weak-library',
    'disable-opts',
])
filename, out = args

source = open(filename).read()
weak_library = False

bcdb_uri = 'sqlite:' + out + '.bcdb'
subprocess.check_call(('bcdb', 'init', '-uri', bcdb_uri))

modules = set(re.findall(r'MODULE\d+', source))
if not modules:
    modules = ['MODULE']
modules = list(modules)
modules.sort()
for module in modules:
    subprocess.check_call('cpp -D%s=1 -P -w < %s | bcdb add -uri %s -name %s -'%(module.upper(), filename, bcdb_uri, module.lower()), shell=True)

clang_args = []
args = ['bcdb', 'mux2', '-uri', bcdb_uri, '-o', out+'.bc']
args.extend(['--muxed-name=muxed.so'])
for o, a in optlist:
    if o == '--weak-library':
        args.append('--weak-name=weak.so')
        weak_library = True
    elif o == '-O':
        clang_args.append(o+a)
    else:
        args.append(o)
args.extend([module.lower() for module in modules])
subprocess.check_call(args)

if not os.path.exists(out+'.elf'):
    os.mkdir(out+'.elf')
if weak_library:
    weak_library = out+'.elf/weak0.so'
    subprocess.check_call(('clang++', '-xir', out+'.bc/weak.so', '-xnone', '-O0', '-o', weak_library, '-fPIC', '-shared', '-w'))
    for i in range(1, 4):
        subprocess.check_call(('clang', '-shared', weak_library, '-o', '%s.elf/weak%d.so'%(out,i)))
        weak_library = '%s.elf/weak%d.so'%(out,i)

args = subprocess.check_output(('bc-imitate', 'clang-args', out+'.bc/muxed.so'), universal_newlines = True)
args = args.strip().split('\n')
args = ['clang++', '-xir', out+'.bc/muxed.so', '-xnone', '-O0', '-o', out+'.elf/muxed.so', '-w'] + args
args.extend(clang_args)
args += ['-fuse-ld=gold']
if weak_library:
    args += [weak_library]
subprocess.check_call(args)

for module in modules:
    module = module.lower()
    args = subprocess.check_output(('bc-imitate', 'clang-args', '%s.bc/%s'%(out,module)), universal_newlines = True)
    args = args.strip().split('\n')
    args = ['clang++', '-xir', '%s.bc/%s'%(out,module), '-xnone', '-O0', '-o', '%s.elf/%s'%(out,module), '-w'] + args
    args += ['-L', out+'.elf', '-Xlinker', '-rpath='+out+'.elf']
    args += ['-Xlinker', '--allow-shlib-undefined']
    args += [out+'.elf/muxed.so']
    args.extend(clang_args)
    if weak_library:
        args += [weak_library]
    subprocess.check_call(args)
