# SPDX-License-Identifier: MIT
"""Split source pixels into editable depth regions with no spherical projection.

The visible pixels remain unchanged. Hidden regions underneath nearer cards are
extended from nearest pixels of the same original layer, avoiding double images
when the head moves. No new artwork or remote/generative service is used.
"""
from collections import deque
from PIL import Image, ImageDraw

def region_labels(size, regions, count):
    """Rasterize authored whole-object depth ownership in source coordinates."""
    w,h=size
    labels=Image.new('L',(w,h));draw=ImageDraw.Draw(labels)
    for region in regions:
        layer=region['layer']
        if layer not in range(count): raise ValueError('Unknown parallax layer')
        if 'polygon' in region:
            points=[(round(x*(w-1)),round(y*(h-1))) for x,y in region['polygon']]
            draw.polygon(points,fill=layer)
        elif 'ellipse' in region:
            x0,y0,x1,y1=region['ellipse'];draw.ellipse((round(x0*(w-1)),round(y0*(h-1)),round(x1*(w-1)),round(y1*(h-1))),fill=layer)
        else: raise ValueError('Unknown parallax region shape')
    return labels

def split_layers(image, regions, definitions):
    source=image.convert('RGBA'); w,h=source.size
    labels=region_labels((w,h),regions,len(definitions))
    owners=list(labels.get_flattened_data());pixels=list(source.get_flattened_data())
    result=[]
    for index,definition in enumerate(definitions):
        # Multi-source nearest-neighbour extension, bounded by the original image.
        nearest=[-1]*(w*h);queue=deque()
        for at,label in enumerate(owners):
            if label==index: nearest[at]=at;queue.append(at)
        if not queue:
            result.append(Image.new('RGBA',(w,h)));continue
        while queue:
            at=queue.popleft();x=at%w;y=at//w
            for other in ((at-1 if x else -1),(at+1 if x+1<w else -1),(at-w if y else -1),(at+w if y+1<h else -1)):
                if other>=0 and nearest[other]<0:nearest[other]=nearest[at];queue.append(other)
        out=Image.new('RGBA',(w,h));out.putdata([(*pixels[at if owners[at]==index else nearest[at]][:3],255 if owners[at]>=index else 0) for at in range(w*h)])
        result.append(out)
    return result
