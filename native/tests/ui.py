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
                conn,_=listener.accept();conn.settimeout(60)
                with conn:
                    file=conn.makefile('rwb',buffering=0)
                    while True:
                        line=file.readline()
                        if not line:break
                        line=line.decode().rstrip('\r\n');received.append(line)
                        if line.startswith('USER '):file.write(b':test 001 ComicXR :Welcome\r\n:test NOTICE AUTH :Server connection notice\r\n:test 372 ComicXR :Server MOTD before joining\r\n')
                        if line=='JOIN #comicchat':
                            file.write(b':ComicXR!me@local JOIN :#comicchat\r\n:test 353 ComicXR = #comicchat :@ComicXR Anna\r\n:Anna!a@local PRIVMSG #comicchat :# Appears as anna\r\n')
                            file.write(':Anna!a@local PRIVMSG #comicchat :(#G099E119RM1) Hello from 世界\r\n'.encode())
                            file.write(b':test 332 ComicXR #comicchat :Our channel topic\r\n:test NOTICE #comicchat :Channel announcement\r\n:Bob!b@local JOIN :#comicchat\r\n:Bob!b@local PART #comicchat :Leaving now\r\n')
                            file.write(b':Anna!a@local PRIVMSG ComicXR :\x01ACTION private action\x01\r\n:Anna!a@local PRIVMSG ComicXR :(#G099E119RM3) Private thought\r\n:Anna!a@local PRIVMSG ComicXR :Private plain\r\n')
                        if line=='PRIVMSG #field :request backdrop':file.write(b':Anna!a@local PRIVMSG #field :# BDrop2: buckroom.bgb,\r\n')
                        if line=='PRIVMSG #clouds :request backdrop':file.write(b':Anna!a@local PRIVMSG #clouds :# BDrop2: buckroom.bgb,\r\n')
                        if line in ['JOIN #space','JOIN #field','JOIN #yellow','JOIN #clouds']:
                            channel=line.split()[1]
                            file.write(f':ComicXR!me@local JOIN :{channel}\r\n:test 353 ComicXR = {channel} :@ComicXR @Anna\r\n'.encode())
                            if channel=='#space':file.write(b':test 353 ComicXR = #unrelated :@FakeOp\r\n:FakeOp!f@local PRIVMSG #space :# BDrop2: buckroom.bgb,\r\n')
                            if channel=='#field':file.write(b':Anna!a@local PRIVMSG #field :# BDrop2: buckroom.bgb,\r\n')
                            if channel=='#yellow':file.write(b':Anna!a@local PRIVMSG #yellow :# BDrop2: room.bgb,\r\n')
                            file.write(f':Anna!a@local PRIVMSG {channel} :Entered {channel}\r\n'.encode())
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
            app=subprocess.Popen([str(exe),'--seconds','40','--capture-ui',str(out/'chat.png')],env=env,stdout=log,stderr=log);children.append(app)
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
            for command,wire in [('/WHOIS Anna','WHOIS Anna'),('/msg Anna private hello','PRIVMSG Anna :private hello'),('/notice Anna notice hello','NOTICE Anna :notice hello'),('/topic new channel topic','TOPIC #comicchat :new channel topic'),('/away lunch break','AWAY :lunch break'),('/back','AWAY'),('/quote VERSION','VERSION'),('/mode','MODE #comicchat')]:
                fill(mx+160,my+720,command);xdo('key','Return');wait(lambda:wire in received)
            fill(mx+160,my+720,'/query Anna');xdo('key','Return')
            fill(mx+160,my+720,'Private query message');xdo('key','Return')
            wait(lambda:any(line.startswith('PRIVMSG Anna :') and 'Private query message' in line for line in received))
            fill(mx+160,my+720,'/query');xdo('key','Return')
            xdo('key','ctrl+t');time.sleep(.15);xdo('key','ctrl+t')
            def command(value):
                fill(mx+160,my+720,value);xdo('key','Return');time.sleep(.12)
            command('/format off');plain_start=len(received)
            # Exercise the actual type selector, not only the local command.
            click(mx+55,my+720);xdo('key','Home');xdo('key','Down');xdo('key','Return')
            command('Plain whisper');wait(lambda:'PRIVMSG #comicchat :Plain whisper' in received)
            command('/balloon thought');command('Plain thought')
            wait(lambda:'PRIVMSG #comicchat :\x01ACTION thinks Plain thought\x01' in received)
            command('/balloon action');command('Plain action')
            wait(lambda:'PRIVMSG #comicchat :\x01ACTION Plain action\x01' in received)
            command('/balloon shout');command('Plain shout')
            wait(lambda:'PRIVMSG #comicchat :Plain shout' in received)
            command('/SAY Plain speech');wait(lambda:'PRIVMSG #comicchat :Plain speech' in received)
            command('/backdrop auto');command('/join #space');time.sleep(.25)
            command('/join #field');time.sleep(.25);command('/backdrop pastoral')
            command('/raw PRIVMSG #field :request backdrop');time.sleep(.25)
            command('/join #yellow');time.sleep(.25)
            command('/backdrop auto');command('/join #clouds');time.sleep(.25)
            command('/raw PRIVMSG #clouds :request backdrop');time.sleep(.25)
            command('/join #space');time.sleep(.25);command('/backdrop pastoral')
            assert not any('(#G' in line or '# Appears as ' in line or '# BDrop' in line for line in received[plain_start:]),received[plain_start:]
            app.wait(timeout=55)
        assert app.returncode==0,(out/'app.log').read_text()
        listener.close()
        state=json.loads((out/'chat.png.json').read_text());messages=[m for p in state['panels'] for m in p['messages']]
        assert any(m['character']=='anna' and '世界' in m['text'] and m['cooked'] for m in messages),messages
        assert any(m['nick']=='ComicXR' and m['text']=='Hello native cutouts' and m['emotion']!=9 for m in messages),messages
        assert any('(#G' in line and 'Hello native cutouts' in line for line in received),received
        output=state['text_output']
        for expected in ['Server connection notice','Server MOTD before joining','Our channel topic','Channel announcement','* Bob joined','* Bob left (Leaving now)','Anna: Hello from 世界','ComicXR: Hello native cutouts']:
            assert expected in output,(expected,output)
        for channel,backdrop in [('#space','space'),('#field','pastoral'),('#yellow','room'),('#clouds','buckroom')]:
            panel=next(p for p in state['panels'] if any(m['text']=='Entered '+channel for m in p['messages']))
            assert panel['backdrop']==backdrop,(channel,panel)
        assert state['chat_options']=={'send_comic_formatting':False,'manual_backdrop':'pastoral','backdrop':'pastoral','balloon_mode':4},state['chat_options']
        for body,mode in [('Plain whisper',2),('Plain thought',3),('Plain action',5),('Plain shout',4),('Plain speech',1)]:
            assert any(m['text']==body and m['mode']==mode for m in messages),(body,messages)
        for body,mode in [('private action',5),('Private thought',3),('Private plain',2)]:
            assert any(m['text']==body and m['mode']==mode for m in messages),(body,messages)
        preferences=json.loads(Path(directory+'/config/comic-chat-xr/preferences.json').read_text())
        assert not preferences['send_comic_formatting'] and preferences['manual_backdrop']=='pastoral' and preferences['balloon_mode']==4,preferences
        restored=subprocess.run([str(exe),'--demo','--seconds','1','--capture-ui',str(out/'preferences.png')],env=env,capture_output=True,text=True,timeout=20)
        assert restored.returncode==0,restored.stderr
        restored_options=json.loads((out/'preferences.png.json').read_text())['chat_options']
        assert restored_options==state['chat_options'],restored_options
        assert json.loads((out/'preferences.png.json').read_text())['panels'][-1]['backdrop']=='pastoral'
        assert state['presentation']['view']=='text',state['presentation']
        assert not any('Server MOTD' in m['text'] for m in messages),'Server output must not become speech balloons'

        im=Image.open(out/'chat.png').convert('RGB');crop=im.crop((15,240,850,460));assert sum(max(p)<50 for p in crop.get_flattened_data())>5000
        print('PASS Connect dialog, actual IRC JOIN, original avatar announcements and expression annotation, Unicode reception, emotion wheel, Enter sends, GPU pixels and teardown')
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:child.kill();child.wait()
