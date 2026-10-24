#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Guard whole-object ownership and exact original backdrop reconstruction."""
import json,math,sys
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from parallax import region_labels,split_layers
art=Path(sys.argv[1]);catalog=json.loads((art/'catalog.json').read_text())
config=json.loads((root/'backdrop-layers.json').read_text())
landmarks=json.loads((Path(__file__).with_name('backdrop-landmarks.json')).read_text())
assert set(config['backdrops'])==set(catalog['backdrops'])==set(landmarks)
assert [layer['depth'] for layer in config['layers']]==sorted((layer['depth'] for layer in config['layers']),reverse=True)
probes=0
for name,regions in config['backdrops'].items():
    original=Image.open(art/catalog['backdrops'][name]['image']).convert('RGBA')
    for region in regions:
        assert region.get('object'),(name,'Missing semantic object name')
        assert region['layer'] in range(len(config['layers']))
        coordinates=region.get('polygon',[region.get('ellipse',[])])
        assert all(math.isfinite(x) and 0<=x<=1 for pair in coordinates for x in pair),(name,region)
    labels=region_labels(original.size,regions,len(config['layers']))
    for probe in landmarks[name]:
        actual=labels.getpixel(tuple(probe['pixel']))
        assert actual==probe['layer'],(name,probe['object'],probe['pixel'],actual,probe['layer'])
        probes+=1
    layers=split_layers(original,regions,config['layers'])
    composition=Image.new('RGBA',original.size)
    for layer in layers:composition=Image.alpha_composite(composition,layer)
    assert composition.tobytes()==original.tobytes(),(name,'Original art changed')
    # The installed catalog must point at the regenerated authored layers.
    for image,record in zip(layers,catalog['backdrops'][name]['layers']):
        installed=Image.open(art/record['image']).convert('RGBA')
        assert installed.tobytes()==image.tobytes(),(name,'Stale imported depth regions')
print(f'PASS {len(landmarks)} source-faithful backdrops, {probes} semantic depth landmarks and exact pixel reconstruction')
