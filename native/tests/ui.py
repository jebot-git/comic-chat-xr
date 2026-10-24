#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real FLTK gestures against an isolated IRC server and X11 fallback display."""
import json,os,signal,socket,subprocess,sys,tempfile,threading,time
from pathlib import Path
from PIL import Image
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('COMIC_UI_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'COMIC_UI_BUS':'1'})
exe=Path(sys.argv[1]).resolve();out=exe.parent/'ui-qa';out.mkdir(exist_ok=True);children=[];received=[];errors=[]
def wait(fn,seconds=15):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        value=fn()
        if value:return value
        time.sleep(.05)
    raise AssertionError('Timed out waiting for UI/server')
try:
    with tempfile.TemporaryDirectory(prefix='comic-ui-') as directory:
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(30)
        port=listener.getsockname()[1]
        def server():
            try:
                conn,_=listener.accept();conn.settimeout(10)
                with conn:
                    file=conn.makefile('rwb',buffering=0)
                    while True:
                        line=file.readline()
                        if not line:break
                        line=line.decode().rstrip('\r\n');received.append(line)
                        if line.startswith('USER '):file.write(b':test 001 ComicXR :Welcome\r\n')
                        if line=='JOIN #comicchat':
                            file.write(b':ComicXR!me@local JOIN :#comicchat\r\n:test 353 ComicXR = #comicchat :@ComicXR Anna\r\n:Anna!a@local PRIVMSG #comicchat :# Appears as anna\r\n')
                            file.write(':Anna!a@local PRIVMSG #comicchat :(#G099E119RM1) Hello from 世界\r\n'.encode())
            except Exception as error:errors.append(str(error))
        thread=threading.Thread(target=server,daemon=True);thread.start()
        r,w=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(w),'-screen','0','1400x1000x24','-nolisten','tcp'],pass_fds=(w,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(w)
        with os.fdopen(r) as stream:display=':'+stream.readline().strip()
        env={**os.environ,'DISPLAY':display,'FLTK_BACKEND':'x11','XDG_CURRENT_DESKTOP':'Dome','XDG_CONFIG_HOME':directory+'/config','XDG_CACHE_HOME':directory+'/cache','LIBGL_ALWAYS_SOFTWARE':'1'}
        env.pop('WAYLAND_DISPLAY',None)
        def xdo(*args):return subprocess.check_output(['xdotool',*map(str,args)],env=env,text=True).strip()
        def click(x,y):xdo('mousemove',x,y,'click',1)
        def fill(x,y,value):
            click(x,y);xdo('key','ctrl+a')
            if value:xdo('type','--clearmodifiers','--delay',1,value)
            else:xdo('key','BackSpace')
        with (out/'app.log').open('w') as log:
            app=subprocess.Popen([str(exe),'--seconds','12','--capture-ui',str(out/'chat.png')],env=env,stdout=log,stderr=log);children.append(app)
            main=wait(lambda:xdo('search','--name','^Comic Chat XR$') if subprocess.run(['xdotool','search','--name','^Comic Chat XR$'],env=env,stdout=subprocess.DEVNULL).returncode==0 else None)
            geometry=dict(line.split('=',1) for line in xdo('getwindowgeometry','--shell',main.splitlines()[-1]).splitlines() if '=' in line)
            mx,my=int(geometry['X']),int(geometry['Y']);time.sleep(.3)
            click(mx+55,my+20)
            dialog=wait(lambda:subprocess.run(['xdotool','search','--name','^Connect to IRC$'],env=env,capture_output=True,text=True).stdout.strip())
            geometry=dict(line.split('=',1) for line in xdo('getwindowgeometry','--shell',dialog.splitlines()[-1]).splitlines() if '=' in line)
            ox,oy=int(geometry['X']),int(geometry['Y'])
            fill(ox+220,oy+35,'127.0.0.1');fill(ox+220,oy+75,'ComicXR');fill(ox+220,oy+115,'');fill(ox+180,oy+155,str(port));click(ox+290,oy+155);click(ox+330,oy+295)
            wait(lambda:any(line.startswith('USER ') for line in received));time.sleep(.25)
            assert not any(line.startswith('JOIN ') for line in received),received
            fill(mx+160,my+720,'/join #comicchat');xdo('key','Return')
            wait(lambda:any('Appears as' in line for line in received))
            click(mx+1043,my+560);fill(mx+160,my+720,'Hello native cutouts');xdo('key','Return')
            wait(lambda:any('Hello native cutouts' in line for line in received))
            xdo('key','ctrl+t');time.sleep(.15);xdo('key','ctrl+t')
            app.wait(timeout=25)
        assert app.returncode==0,(out/'app.log').read_text()
        listener.close()
        state=json.loads((out/'chat.png.json').read_text());messages=[m for p in state['panels'] for m in p['messages']]
        assert any(m['character']=='anna' and '世界' in m['text'] and m['cooked'] for m in messages),messages
        assert any(m['nick']=='ComicXR' and m['text']=='Hello native cutouts' and m['emotion']!=9 for m in messages),messages
        assert any('(#G' in line and 'Hello native cutouts' in line for line in received),received
        im=Image.open(out/'chat.png').convert('RGB');crop=im.crop((15,240,850,460));assert sum(max(p)<50 for p in crop.get_flattened_data())>5000
        print('PASS Connect dialog, actual IRC JOIN, original avatar announcements and expression annotation, Unicode reception, emotion wheel, Enter sends, GPU pixels and teardown')
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:child.kill();child.wait()
