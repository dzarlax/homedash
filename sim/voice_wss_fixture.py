#!/usr/bin/env python3
"""Local TLS/WebSocket fixture; exercises wire/model, not ESP's transport implementation."""
import base64, hashlib, json, os, socket, ssl, struct, subprocess, sys, tempfile, threading

def exact(sock, n):
    out = b''
    while len(out) < n:
        chunk = sock.recv(n - len(out))
        if not chunk: raise RuntimeError('unexpected EOF')
        out += chunk
    return out

def frame(op, fin, data):
    prefix=bytes([(0x80 if fin else 0) | op])
    return prefix + (bytes([len(data)]) if len(data)<126 else bytes([126])+struct.pack('!H',len(data))) + data

def snapshot(rev, turn, phase, text):
    return json.dumps(dict(v=1,type='voice.snapshot',lease_id='synthetic',revision=rev,
        session_id='session',turn_id=turn,phase=phase,backend_active=False,playback='unknown',
        text=text,text_truncated=False,interrupted=False,active=True,ttl_ms=60000,replay=False),ensure_ascii=False,separators=(',',':')).encode()

def main():
    with tempfile.TemporaryDirectory(prefix='voice-wss-') as tmp:
        key, cert = os.path.join(tmp,'key.pem'), os.path.join(tmp,'cert.pem')
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1',
            '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-keyout',key,'-out',cert],
            check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(2)
        listener.settimeout(10); port=listener.getsockname()[1]
        server_context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); server_context.load_cert_chain(cert,key)
        errors=[]
        def server():
            try:
                raw,_=listener.accept()
                try: server_context.wrap_socket(raw,server_side=True).close()
                except ssl.SSLError: raw.close()
                raw,_=listener.accept()
                with server_context.wrap_socket(raw,server_side=True) as conn:
                    req=b''
                    while b'\r\n\r\n' not in req: req+=conn.recv(4096)
                    assert b'GET /api/voice/v1/events HTTP/1.1' in req
                    assert b'Authorization: Bearer synthetic-only\r\n' in req
                    wskey=next(line.split(b': ',1)[1] for line in req.split(b'\r\n') if line.lower().startswith(b'sec-websocket-key:'))
                    accept=base64.b64encode(hashlib.sha1(wskey+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
                    conn.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
                    one=snapshot(1,'turn-1','listening',''); split=len(one)//2
                    messages=[frame(1,False,one[:split]),frame(0,True,one[split:]),
                        frame(1,True,snapshot(2,'turn-1','answer','Привет')),
                        frame(1,True,snapshot(1,'turn-1','listening','')),
                        frame(1,True,snapshot(3,'turn-2','listening','')),
                        frame(1,True,b'{"v":1,"type":"voice.clear","lease_id":"synthetic","revision":3,"reason":"expired"}')]
                    for message in messages: conn.sendall(message)
            except Exception as exc: errors.append(exc)
            finally: listener.close()
        thread=threading.Thread(target=server); thread.start()
        context=ssl.create_default_context(cafile=cert)
        try:
            with context.wrap_socket(socket.create_connection(('127.0.0.1',port)),server_hostname='wrong.invalid'): pass
            raise AssertionError('hostname mismatch was accepted')
        except ssl.SSLCertVerificationError: pass
        replay=subprocess.Popen([sys.argv[1]],stdin=subprocess.PIPE)
        with context.wrap_socket(socket.create_connection(('127.0.0.1',port)),server_hostname='localhost') as conn:
            key64=base64.b64encode(os.urandom(16)); conn.sendall(b'GET /api/voice/v1/events HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: '+key64+b'\r\nAuthorization: Bearer synthetic-only\r\n\r\n')
            header=b''
            while not header.endswith(b'\r\n\r\n'): header+=exact(conn,1)
            expected=base64.b64encode(hashlib.sha1(key64+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
            assert b'101 Switching Protocols' in header and expected in header
            for _ in range(6):
                h=exact(conn,2); op=h[0]&15; fin=bool(h[0]&128); assert not (h[1]&128)
                n=h[1]&127
                if n==126: n=struct.unpack('!H',exact(conn,2))[0]; assert n>=126
                assert n<65536; data=exact(conn,n)
                for off in range(0,n,7):
                    chunk=data[off:off+7]; replay.stdin.write(struct.pack('!BBIII',op,fin,n,off,len(chunk))+chunk)
        replay.stdin.close(); assert replay.wait(timeout=10)==0
        thread.join(timeout=10); assert not thread.is_alive() and not errors,errors
if __name__=='__main__': main()
