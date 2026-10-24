#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import json, socket, subprocess, sys, tempfile, threading, time
from pathlib import Path
exe=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='comic-irc-') as folder:
    listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(); listener.settimeout(10)
    received=[]; errors=[]
    def server():
        try:
            conn,_=listener.accept(); conn.settimeout(8)
            with conn:
                file=conn.makefile('rwb',buffering=0)
                while True:
                    line=file.readline().decode().rstrip('\r\n');received.append(line)
                    if line.startswith('USER '): break
                file.write(b':local 001 ComicXR :Welcome\r\nPING :probe-token\r\n')
                # Split a multibyte UTF-8 sequence and IRC line over separate reads.
                line=':Anna!a@local PRIVMSG #comicchat :Hello 世界\r\n'.encode()
                file.write(line[:-5]);time.sleep(.03);file.write(line[-5:])
                while len(received)<5:
                    line=file.readline()
                    if not line:break
                    received.append(line.decode().rstrip('\r\n'))
                time.sleep(1)
        except Exception as error: errors.append(str(error))
    thread=threading.Thread(target=server);thread.start()
    output=Path(folder)/'events.json'
    probe=subprocess.run([exe,'--irc-probe','127.0.0.1',str(listener.getsockname()[1]),'--no-tls','--seconds','2','--probe-output',str(output)],capture_output=True,text=True,timeout=10)
    thread.join(timeout=9);listener.close()
    assert probe.returncode==0,(probe.stdout,probe.stderr,received,errors)
    assert not errors,errors
    assert 'NICK ComicXR' in received and 'JOIN #comicchat' in received,received
    assert 'PONG :probe-token' in received,received
    assert 'PRIVMSG #comicchat :probe roundtrip' in received,received
    events=json.loads(output.read_text())
    assert any('Hello 世界' in e.get('line','') for e in events),events
    print('PASS real TCP registration, JOIN, PING/PONG, outbound message, fragmented Unicode and teardown')

# The default TLS connection must fail on an untrusted certificate, without
# sending registration or credentials into a session whose identity is unknown.
import ssl
with tempfile.TemporaryDirectory(prefix='comic-tls-') as directory:
    root=Path(directory);key=root/'key.pem';cert=root/'cert.pem';events=root/'events.json'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(key),'-out',str(cert),'-days','1','-subj','/CN=localhost'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(10)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key);wire=[]
    def untrusted():
        raw,_=listener.accept();raw.settimeout(8)
        try:
            with context.wrap_socket(raw,server_side=True) as conn:wire.append(conn.recv(1024))
        except ssl.SSLError:raw.close()
    thread=threading.Thread(target=untrusted,daemon=True);thread.start()
    result=subprocess.run([exe,'--irc-probe','127.0.0.1',str(listener.getsockname()[1]),'--seconds','2','--probe-output',str(events)],capture_output=True,text=True,timeout=10)
    thread.join(timeout=8);listener.close()
    assert result.returncode!=0 and not wire,(result.stdout,result.stderr,wire)
    assert any(e.get('type')=='status' and 'Disconnected:' in e.get('text','') for e in json.loads(events.read_text()))
    print('PASS untrusted TLS certificate rejected before IRC registration')
