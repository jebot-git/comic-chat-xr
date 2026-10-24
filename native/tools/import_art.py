#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Microsoft AVB/BGB records translated from v2.5-beta-1-modern/avbfile.{h,cpp}.
import argparse, hashlib, io, json, struct, zlib
from pathlib import Path
from PIL import Image
from parallax import split_layers

MAX_FILE = 32 * 1024 * 1024
MAX_IMAGE = 16 * 1024 * 1024

class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0
    def take(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError('Truncated art record')
        b = self.data[self.pos:self.pos+n]; self.pos += n
        return b
    def unpack(self, fmt):
        return struct.unpack('<'+fmt, self.take(struct.calcsize('<'+fmt)))
    def word(self): return self.unpack('H')[0]
    def string(self):
        n = self.data.find(b'\0', self.pos, min(self.pos+8192,len(self.data)))
        if n < 0: raise ValueError('Unterminated art string')
        return self.take(n-self.pos+1)[:-1].decode('cp1252', errors='replace')
    def palette(self):
        n = self.word()
        if n > 256: raise ValueError('Oversized palette')
        return [tuple(self.take(3)) for _ in range(n)]

class Art:
    def __init__(self, path):
        data = path.read_bytes()
        if len(data) > MAX_FILE: raise ValueError('Art file too large')
        self.path, self.data = path, data
        self.parts = {'bodies':[], 'faces':[], 'torsos':[]}
        self.name, self.flags, self.palette, self.adjust = path.stem, 0, [], 0
        self.copyright = self.url = ''
        r = Reader(data)
        magic, self.kind, version = r.unpack('HHH')
        if magic not in (0x81, 0x8181) or self.kind not in (1,2,3) or version > 2:
            raise ValueError('Unsupported AVB header')
        self.backdrop = None
        for _ in range(4096):
            tag = r.word(); size = r.word() if tag >= 256 else None
            end = r.pos + size if size is not None else None
            if tag == 6: break
            if tag == 1: self.name = r.string()
            elif tag == 2: self.flags = r.word()
            elif tag == 8: r.word()
            elif tag == 3: r.take(4)
            elif tag == 256: r.take(6)
            elif tag == 257: self.palette = r.palette()
            elif tag in (259,260,261):
                value = r.string()
                if tag == 259: self.copyright = value
                elif tag == 260: self.url = value
            elif tag == 263: self.adjust += r.unpack('i')[0]
            elif tag == 258:
                offset, fmt, pal = r.unpack('IBB')
                self.backdrop = (offset+self.adjust, fmt, pal)
            elif tag in (4,5,9,10,11,12):
                count = r.word()
                if count > 4096: raise ValueError('Too many poses')
                group = 'faces' if tag in (4,10) else 'torsos' if tag in (5,11) else 'bodies'
                old = tag in (4,5,9)
                fields = 6 if group=='faces' else 2
                for _ in range(count):
                    offsets = r.unpack('III'); emotion, intensity = r.unpack('HB')
                    coords = r.unpack('h'*fields)
                    if old:
                        r.take(16); formats=(0,0,0); palettes=(0,0,0)
                    else:
                        formats = r.unpack('BBB'); palettes = r.unpack('BBB')
                    record = {'emotion':emotion,'intensity':intensity/255,
                              'coords':coords,'resources':[(o+self.adjust if o else 0,f,p) for o,f,p in zip(offsets,formats,palettes)]}
                    self.parts[group].append(record)
            elif end is None: raise ValueError(f'Unknown legacy tag {tag}')
            if end is not None:
                if r.pos > end: raise ValueError('Record exceeds declared size')
                r.take(end-r.pos)
        else: raise ValueError('Art header record limit')

    def image(self, resource):
        offset, fmt, paltype = resource
        if offset <= 0 or offset >= len(self.data): raise ValueError('Invalid image offset')
        r = Reader(self.data); r.pos = offset
        if fmt == 0:
            im = Image.open(io.BytesIO(self.data[offset:])); im.load()
            return im.convert('RGBA'), im.convert('L')
        if fmt != 1: raise ValueError('Unsupported art compression')
        palette = self.palette if paltype == 1 else [(255,255,255),(0,0,0)] if paltype == 3 else [(255,255,255),(0,0,0),(128,0,0),(0,0,128)] if paltype in (4,5) else []
        if paltype == 2:
            tag, size = r.unpack('HH')
            if tag != 257: raise ValueError('Missing local palette')
            palette = r.palette()
        start = r.pos; header = r.unpack('IiiHHIIiiII'); hsize,w,h,planes,bits,compression,_,_,_,_,_ = header
        if not 40 <= hsize <= 240 or not 0 < w <= 4096 or not 0 < abs(h) <= 4096 or bits not in (1,2,4,8,24,32) or planes != 1 or compression != 0:
            raise ValueError('Unsupported or oversized art bitmap')
        r.take(hsize - (r.pos-start))
        rawlen, complen = r.unpack('II')
        stride = ((w*bits+31)//32)*4
        if rawlen != stride*abs(h) or rawlen > MAX_IMAGE or complen > MAX_IMAGE:
            raise ValueError('Art bitmap storage mismatch')
        dec = zlib.decompressobj()
        raw = dec.decompress(r.take(complen), rawlen+1)
        if len(raw) != rawlen or not dec.eof or dec.unconsumed_tail:
            raise ValueError('Invalid bounded zlib image')
        rgba = bytearray(w*abs(h)*4); indices = bytearray(w*abs(h))
        for y in range(abs(h)):
            row = raw[(abs(h)-1-y if h>0 else y)*stride:]
            for x in range(w):
                if bits <= 8:
                    ix = (row[(x*bits)//8] >> (8-bits-(x*bits)%8)) & ((1<<bits)-1)
                    if ix >= len(palette): raise ValueError('Palette index out of range')
                    rgb = palette[ix]; indices[y*w+x]=ix
                else: rgb = tuple(row[x*(bits//8):x*(bits//8)+3][::-1])
                at=(y*w+x)*4; rgba[at:at+4]=bytes((*rgb,255))
        return Image.frombytes('RGBA',(w,abs(h)),bytes(rgba)), Image.frombytes('L',(w,abs(h)),bytes(indices))

    def pose(self, record):
        resources = record['resources']; image, indices = self.image(resources[0])
        if resources[0][2] == 4:
            # Match CPose::ConvertMasksCommon and GDI SRCAND: 11 black,
            # 10 white, 01 white aura, 00 transparent. The legacy header comment
            # reverses black/white; the conversion implementation is authoritative.
            image = Image.new('RGBA', image.size)
            image.putdata([(0,0,0,255) if x==3 else (255,255,255,255) if x else (255,255,255,0) for x in indices.get_flattened_data()])
            aura = indices.point(lambda x: 255 if x else 0)
        elif resources[1][0]:
            mask, values = self.image(resources[1])
            if mask.size != image.size: raise ValueError('Pose mask size mismatch')
            # GDI MERGEPAINT whitened wherever the inverted mask was white.
            alpha = values.point(lambda x: 255 if x&1 else 0) if resources[1][2]==5 else mask.convert('L').point(lambda x:255-x)
            image.putalpha(alpha)
            aura = self.image(resources[2])[0].convert('L').point(lambda x:255-x) if resources[2][0] else alpha
        else:
            # Single monochrome body uses SRCAND; white is transparent.
            alpha = image.convert('L').point(lambda x:255 if x<250 else 0)
            image.putalpha(alpha); aura = alpha
        return image, aura

def import_all(source, output):
    output.mkdir(parents=True, exist_ok=True)
    catalog={'format':1,'characters':{},'backdrops':{}}
    paths = {p.stem.lower():p for p in sorted((source/'comicart').glob('*.avb'))}
    for p in sorted((source/'artpack1').glob('*.avb')): paths.setdefault(p.stem.lower(),p)
    for key,path in paths.items():
        art=Art(path); character={'name':art.name,'flags':art.flags,'type':art.kind,'copyright':art.copyright,'url':art.url,'sha256':hashlib.sha256(art.data).hexdigest()}
        folder=output/'characters'/key; folder.mkdir(parents=True,exist_ok=True)
        cache={}
        for group, records in art.parts.items():
            character[group]=[]
            for n,rec in enumerate(records):
                resource=tuple(map(tuple,rec['resources']))
                if resource not in cache:
                    im,aura=art.pose(rec)
                    filename=f'{group}-{n}.png'; im.save(folder/filename)
                    aura.convert('RGBA').save(folder/(filename+'.aura.png'))
                    cache[resource]=(f'characters/{key}/{filename}',im.size)
                filename, dims=cache[resource]
                character[group].append({k:v for k,v in rec.items() if k!='resources'} | {'image':filename,'width':dims[0],'height':dims[1]})
        catalog['characters'][key]=character
        print(f'{key}: '+', '.join(f'{len(v)} {k}' for k,v in art.parts.items()),flush=True)
    depth=json.loads((Path(__file__).parents[1]/'backdrop-layers.json').read_text())
    for path in sorted((source/'comicart').glob('*.bgb')):
        art=Art(path)
        if not art.backdrop: raise ValueError('Missing backdrop')
        im,_=art.image(art.backdrop); folder=output/'backdrops'; folder.mkdir(exist_ok=True)
        im.save(folder/(path.stem+'.png'))
        layers=[]
        for definition,layer in zip(depth['layers'],split_layers(im,depth['backdrops'][path.stem],depth['layers'])):
            filename=f"{path.stem}.{definition['name']}.png";layer.save(folder/filename)
            layers.append({**definition,'image':f'backdrops/{filename}'})
        catalog['backdrops'][path.stem]={'layers':layers,'image':f'backdrops/{path.stem}.png','width':im.width,'height':im.height,'copyright':art.copyright,'sha256':hashlib.sha256(art.data).hexdigest()}
    (output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
    print(f"Imported {len(catalog['characters'])} characters and {len(catalog['backdrops'])} backdrops")
    return catalog

if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('source',type=Path); p.add_argument('output',type=Path)
    a=p.parse_args(); import_all(a.source,a.output)
