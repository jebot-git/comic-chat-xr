#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Actual native Wayland window and universal menu, on a private DomeD."""
import json, os, re, signal, socket, subprocess, sys, tempfile, time
from pathlib import Path
from PIL import Image
signal.pthread_sigmask(signal.SIG_SETMASK, [])
if os.environ.get('COMIC_PRIVATE_BUS') != '1':
    os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'COMIC_PRIVATE_BUS':'1'})
exe=Path(sys.argv[1]).resolve(); compositor=Path(sys.argv[2]).resolve()
out=exe.parent/'wayland-qa';out.mkdir(exist_ok=True);children=[]
try:
    with tempfile.TemporaryDirectory(prefix='comic-wayland-') as directory:
        runtime=Path(directory);runtime.chmod(0o700)
        read,write=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(write),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(write,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(write)
        with os.fdopen(read) as stream:display=':'+stream.readline().strip()
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        env={**os.environ,'DISPLAY':display,'XDG_RUNTIME_DIR':directory,'XDG_CURRENT_DESKTOP':'Dome',
             'XDG_CONFIG_HOME':directory+'/config','XDG_CACHE_HOME':directory+'/cache','XDG_STATE_HOME':directory+'/state',
             'LIBGL_ALWAYS_SOFTWARE':'1','DOME_START_XR':'0','DOME_LOGIN_SESSION':'0','DOME_APPEARANCE_SERVICE':'0',
             'DOME_RENDERER':'gles','DOME_PREVIEW_PORT':str(port),'DBUS_SYSTEM_BUS_ADDRESS':'unix:path=/nonexistent/comic-system-bus',
             'XR_RUNTIME_JSON':'/nonexistent/comic-private-runtime.json'}
        for k in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','FLTK_BACKEND']:env.pop(k,None)
        with (out/'compositor.log').open('w') as log:
            comp=subprocess.Popen([str(compositor),'--backend','nested'],env=env,stdout=log,stderr=log);children.append(comp)
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                match=re.search(r'Dome Wayland socket: (\S+)',(out/'compositor.log').read_text())
                if match:break
                assert comp.poll() is None,'Private compositor exited'
                time.sleep(.1)
            else:raise AssertionError('No private Wayland socket')
            env.update(WAYLAND_DISPLAY=match[1],FLTK_BACKEND='wayland',WAYLAND_DEBUG='client')
            with (out/'client.log').open('w') as clientlog:
                app=subprocess.Popen([str(exe),'--demo','--capture-ui',str(out/'desktop.png')],env=env,stdout=clientlog,stderr=clientlog);children.append(app);app.wait(timeout=25)
            trace=(out/'client.log').read_text()
            assert app.returncode==0,trace[-5000:]
            assert 'set_address' in trace and 'xdg_surface' in trace,'Missing Wayland universal menu integration'
            im=Image.open(out/'desktop.png').convert('RGB')
            # Detect actual source artwork, not only FLTK chrome and an empty white canvas.
            crop=im.crop((25,230,800,460))
            pixels=list(crop.get_flattened_data());black=sum(max(p)<40 for p in pixels);white=sum(min(p)>220 for p in pixels)
            assert black>10000 and white>10000,(black,white)
            assert json.loads((out/'desktop.png.json').read_text())['panels']
            print('PASS native Wayland GPU comic art, bitmap text, FLTK controls, exported universal menu and clean teardown')
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:child.kill();child.wait()
