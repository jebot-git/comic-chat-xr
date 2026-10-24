#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import json, os, subprocess, sys, tempfile
from pathlib import Path
with tempfile.TemporaryDirectory(prefix='comic-stereo-') as directory:
    root=Path(directory); model=root/'scene.glb';report=root/'report.json';runtime=root/'runtime.json'
    subprocess.run([sys.argv[1],'--demo','--export',str(model)],check=True,capture_output=True)
    env={**os.environ,'XDG_CURRENT_DESKTOP':'StandaloneTest','WAYLAND_DISPLAY':'','LD_PRELOAD':str(Path(sys.argv[2]).resolve()),'LIBGL_ALWAYS_SOFTWARE':'1',
         'XR_RUNTIME_JSON':'/nonexistent/comic-isolated-runtime.json','FSVR_MOCK_REPORT':str(runtime)}
    result=subprocess.run([sys.argv[1],'--xr',str(model),'--seconds','2','--report',str(report)],env=env,capture_output=True,text=True,timeout=15)
    assert result.returncode==0,result.stdout+result.stderr
    data=json.loads(report.read_text());trace=json.loads(runtime.read_text())
    assert data['overlay'] and data['rendered_stereo_frames']>20,data
    assert data['position']==[0,0,0] and data['scale']==1,data
    assert data['grab_frames']==0,data
    assert trace['placement']==5,trace
    assert trace['stereo_frames']==data['rendered_stereo_frames'],(trace,data)
    print('PASS real stereo GPU render targets, room anchored scene, no scale normalization, backdrop below DomeD, controller tracking and clean close')
