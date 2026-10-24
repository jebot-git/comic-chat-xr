#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real private DomeD sockets and mode switch; isolated OpenXR GPU frames."""
import json,os,re,signal,socket,subprocess,sys,tempfile,threading,time,urllib.request
from pathlib import Path
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('COMIC_MODE_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'COMIC_MODE_BUS':'1'})
exe,compositor,mock=map(lambda x:Path(x).resolve(),sys.argv[1:4]);out=exe.parent/'mode-qa';out.mkdir(exist_ok=True)
children=[];logs=[];stop=threading.Event()
def wait(fn,seconds=12):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        try:
            value=fn()
            if value:return value
        except (OSError,ValueError,KeyError,StopIteration):pass
        time.sleep(.05)
    raise AssertionError('Mode test timed out; inspect '+str(out))
def start(args,env,name):
    log=(out/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen(list(map(str,args)),env=env,stdout=log,stderr=log);children.append(p);return p
try:
    with tempfile.TemporaryDirectory(prefix='comic-mode-') as directory:
        root=Path(directory);root.chmod(0o700)
        rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(wr)
        with os.fdopen(rd) as stream:display=':'+stream.readline().strip()
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        env={**os.environ,'DISPLAY':display,'XDG_RUNTIME_DIR':directory,'XDG_CURRENT_DESKTOP':'Dome','XDG_CONFIG_HOME':directory+'/config','XDG_CACHE_HOME':directory+'/cache','XDG_STATE_HOME':directory+'/state','XDG_DATA_HOME':directory+'/data','DOME_START_XR':'0','DOME_START_IME':'0','DOME_LOGIN_SESSION':'0','DOME_SESSION_RESTORE':'0','DOME_APPEARANCE_SERVICE':'0','DOME_START_XWAYLAND':'0','DOME_RENDERER':'gles','LIBGL_ALWAYS_SOFTWARE':'1','DOME_PREVIEW_PORT':str(port),'XR_RUNTIME_JSON':'/nonexistent/comic-mode-runtime.json','DBUS_SYSTEM_BUS_ADDRESS':'unix:path=/nonexistent/comic-system-bus'}
        for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','FLTK_BACKEND']:env.pop(key,None)
        comp=start([compositor,'--backend','nested'],env,'compositor')
        desktop=wait(lambda:re.search(r'Dome Wayland socket: (\S+)',(out/'compositor.log').read_text()))[1]
        context=json.loads((root/(desktop+'.dome-session.json')).read_text());xr=context['xr_socket']
        env.update(WAYLAND_DISPLAY=desktop,FLTK_BACKEND='wayland')
        http=urllib.request.build_opener(urllib.request.ProxyHandler({}));base=f'http://127.0.0.1:{port}'
        def get(path):return json.load(http.open(base+path,timeout=2))
        def post(path,data):return json.load(http.open(urllib.request.Request(base+path,data=json.dumps(data).encode(),headers={'Content-Type':'application/json'}),timeout=2))
        def probe(environment,realm='inherit'):
            result=subprocess.run([str(exe),'--realm',realm,'--session-status'],env=environment,capture_output=True,text=True,timeout=4)
            assert result.returncode==0,result.stderr
            return json.loads(result.stdout)
        assert probe(env)['launch_realm']=='desktop'
        assert probe({**env,'WAYLAND_DISPLAY':xr})['launch_realm']=='xr'
        assert probe({**env,'WAYLAND_DISPLAY':xr},'desktop')['launch_realm']=='desktop'
        assert probe(env,'xr')['launch_realm']=='xr'
        # Actual FLTK windows land in their caller's compositor, not only a probe.
        for realm,wayland in [('desktop',desktop),('xr',xr)]:
            p=start([exe,'--demo','--seconds','1','--capture-ui',out/(realm+'.png')],{**env,'WAYLAND_DISPLAY':wayland},realm)
            window=wait(lambda:next((w for w in get('/api/state')['scene']['windows'] if w['app_id']=='org.de_xr.ComicChat.Sbs'),None))
            assert window['origin']==realm,window
            p.wait(timeout=20)
            state=json.loads((out/(realm+'.png.json')).read_text())['presentation'];assert state['launch_realm']==realm and not state['presenter'],state
            wait(lambda:not any(w['app_id']=='org.de_xr.ComicChat.Sbs' for w in get('/api/state')['scene']['windows']))
        print('PASS desktop/XR launch sockets, actual FLTK origin, explicit sibling selection and no presenter while XR is inactive',flush=True)
        trackingHead={'position_m':[0,1.6,0],'orientation_xyzw':[0,0,0,1]}
        def eyePoses(head):
            x,y,z,w=head['orientation_xyzw'];p=head['position_m']
            right=[1-2*y*y-2*z*z,2*(x*y+w*z),2*(x*z-w*y)]
            return [{'position_m':[p[i]+sign*.032*right[i] for i in range(3)],
                     'orientation_xyzw':head['orientation_xyzw']} for sign in [-1,1]]
        def pulse():
            while not stop.is_set():
                try:post('/api/xr-session',{'id':77,'visible':True,'focused':True,'horizontal_fov_deg':90,'head':trackingHead,'eyes':eyePoses(trackingHead)})
                except OSError:return
                stop.wait(.1)
        threading.Thread(target=pulse,daemon=True).start();wait(lambda:get('/api/xr-control')['visible'])
        def setmode(active):
            post('/api/action',{'op':'xr_presentation','desktop':active})
            time.sleep(.35) # The compositor retains XR input until fade-out completes.
            wait(lambda:get('/api/xr-control')['input_realm']==('xr' if active else 'desktop'))
        setmode(False);initial=probe(env);assert initial['known'] and not initial['xr'],initial
        model=out/'scene.glb';subprocess.run([str(exe),'--demo','--export',str(model)],check=True,capture_output=True)
        frames=out/'frames.txt';frames.write_text('');report=out/'xr.json'
        xrEnv={**env,'LD_PRELOAD':str(mock),'COMIC_MOCK_NO_CLOSE':'1','COMIC_MOCK_FRAME_LOG':str(frames)}
        presenter=start([exe,'--xr',model,'--seconds','9','--report',report],xrEnv,'presenter')
        def counts(since):
            return [int(parts[1]) for line in frames.read_text().splitlines() if len(parts:=line.split())==2 and float(parts[0])>since]
        wait(lambda:frames.stat().st_size>100)
        for active,depth in [(False,1.0),(True,1.2),(False,2.6),(True,2.8)]:
            config=get('/api/state')['scene']['config'];config['pullout_distance_m']=depth;post('/api/action',{'op':'configure','config':config})
            assert abs(probe(env)['pullout_depth_m']-depth)<1e-5
            setmode(active);assert probe(env)['xr']==active;time.sleep(.45);since=time.monotonic();time.sleep(.35);values=counts(since);assert len(values)>5 and all(v==int(active) for v in values),(active,values)
        presenter.wait(timeout=12);assert presenter.returncode==0,(out/'presenter.log').read_text()
        report=json.loads(report.read_text());assert report['mode_hidden_frames']>50 and report['rendered_stereo_frames']>50,report
        assert report['pullout_alignment'] and abs(report['pullout_depth_min_m']-1.2)<1e-5 and abs(report['pullout_depth_max_m']-2.8)<1e-5,report
        print('PASS mode gating and live alignment to changed pull-out depths without restarting the XR session',flush=True)
        setmode(False)
        # The native client replaces the comic with an in-window GL viewport.
        app=start([exe,'--demo','--seconds','7','--capture-ui',out/'automatic.png'],xrEnv,'automatic')
        wait(lambda:any(w['app_id']=='org.de_xr.ComicChat.Sbs' for w in get('/api/state')['scene']['windows']))
        def descendants():
            p=Path(f'/proc/{app.pid}/task/{app.pid}/children');return p.read_text().split() if p.exists() else []
        time.sleep(.4);assert not descendants(),descendants()
        window=next(w for w in get('/api/state')['scene']['windows'] if w['app_id']=='org.de_xr.ComicChat.Sbs')
        assert window['client_pid']==app.pid,window
        # A second instance must not lend its geometry to this client.
        decoy=start([exe,'--demo'],env,'second-instance')
        wait(lambda:any(w.get('client_pid')==decoy.pid for w in get('/api/state')['scene']['windows']))
        import math
        anchor={'position_m':[.25,1.8,.5],'orientation_xyzw':[0,math.sqrt(.5),0,math.sqrt(.5)]}
        post('/api/action',{'op':'pull_out','id':window['id'],'head':anchor});assert probe(env)['shared_pullout_anchor']
        trackingHead=anchor
        setmode(True);time.sleep(.8);assert not descendants(),descendants()
        (out/'stereo-layout.json').write_text(json.dumps(get('/api/integrations/comic-chat'),indent=2))
        alignedReport=out/'aligned.json';aligned=start([exe,'--xr',model,'--seconds','.7','--report',alignedReport],xrEnv,'aligned');aligned.wait(timeout=5);assert aligned.returncode==0
        alignment=json.loads(alignedReport.read_text());m=alignment['scene_root'];point=[sum(m[c*4+r]*v for c,v in enumerate([0,1.6,-2,1])) for r in range(3)]
        assert alignment['shared_pullout_anchor'] and all(abs(a-b)<1e-4 for a,b in zip(point,[-2.55,1.74,.5])),(alignment,point)
        setmode(False);time.sleep(.6);assert not descendants(),descendants()
        setmode(True)
        app.wait(timeout=15);assert app.returncode==0
        presentation=json.loads((out/'automatic.png.json').read_text())['presentation']
        assert presentation['view']=='3d' and presentation['scene_frames']>5 and not presentation['presenter'],presentation
        assert 0<presentation['content_height_m']<2.8*presentation['lane_height_radians'],presentation
        assert presentation['xr_entries']>=2 and presentation['desktop_returns']>=1,presentation
        assert not presentation['scene_error'],presentation
        assert presentation['stereo_supported'] and presentation['tracked_eyes'],presentation
        assert presentation['stereo_frames']>5,presentation
        assert presentation['window_id']==window['id'],presentation
        current=get('/api/state');span=current['spans'][window['lane']]
        assert abs(presentation['lane_height_radians']-math.radians(span[1]-span[0]))<1e-5,presentation
        from PIL import Image
        image=Image.open(out/'automatic.png').convert('RGB')
        # The content rectangle must contain rendered artwork, not an empty GL surface.
        dark=sum(max(pixel)<100 for pixel in image.crop((20,60,850,650)).getdata())
        assert dark>3000,dark
        from PIL import ImageChops,ImageStat
        half=image.width//2
        left=image.crop((0,0,half,image.height));right=image.crop((image.width-half,0,image.width,image.height))
        left.save(out/'left-eye.png');right.save(out/'right-eye.png')
        assert ImageChops.difference(left.crop((0,0,half,40)),right.crop((0,0,half,40))).getbbox() is None,'Chrome must have zero disparity'
        diff=ImageChops.difference(left.crop((5,50,half-120,590)),right.crop((5,50,half-120,590)))
        assert sum(ImageStat.Stat(diff).mean)>1,'Both eyes received the same artwork'
        assert window['stereo']=='side_by_side',window
        decoy.terminate();decoy.wait(timeout=5)
        setmode(False)
        restored=start([exe,'--demo','--seconds','1','--capture-ui',out/'restored.png'],env,'restored')
        restored.wait(timeout=15);assert restored.returncode==0
        restoredState=json.loads((out/'restored.png.json').read_text())['presentation']
        assert restoredState['view']=='comic' and not restoredState['presenter'],restoredState
        print("PASS comic plane shares the pulled window's yaw, height offset and configured depth",flush=True)
        print('PASS in-window stereo artwork with tracked eyes, zero-disparity controls, lane-height fit and no presenter child',flush=True)
finally:
    stop.set()
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:child.kill();child.wait()
    for log in logs:log.close()
