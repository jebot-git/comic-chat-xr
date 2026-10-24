#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import importlib.util,struct,sys,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/"tools"))
spec=importlib.util.spec_from_file_location('import_art',Path(__file__).parents[1]/'tools/import_art.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
# Independent two-bit fixture for the original GDI conversion, not the misleading
# black/white order in AVATARIMAGEPALETTE's historical comment.
raw=bytes([0x1b,0,0,0]);compressed=zlib.compress(raw)
header=struct.pack('<IiiHHIIiiII',40,4,1,1,2,0,4,0,0,0,0)
a=module.Art.__new__(module.Art);a.data=b'X'+header+struct.pack('<II',4,len(compressed))+compressed;a.palette=[]
image,aura=a.pose({'resources':[(1,1,4),(0,0,0),(0,0,0)]})
assert list(image.get_flattened_data())==[(255,255,255,0),(255,255,255,255),(255,255,255,255),(0,0,0,255)]
assert list(aura.get_flattened_data())==[0,255,255,255]
# Oversized compressed payload cannot inflate beyond its declared image storage.
a.data=b'X'+header+struct.pack('<II',4,len(zlib.compress(b'A'*100000)))+zlib.compress(b'A'*100000)
try:a.image((1,1,4))
except ValueError:pass
else:raise AssertionError('Unbounded image inflation accepted')
try:module.Reader(b'\x00').word()
except ValueError:pass
else:raise AssertionError('Truncated record accepted')
print('PASS original masked monochrome colour semantics, aura, truncation and bounded zlib decoding')
