#!/usr/bin/env python3
"""Real packaged core -> loopback IMAP/SMTP. No relay credentials required."""
import json
import os
import queue
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

mail = queue.Queue()
reject = threading.Event()
class SMTP(socketserver.StreamRequestHandler):
    def handle(self):
        def send(line): self.wfile.write(line.encode() + b'\r\n'); self.wfile.flush()
        send('220 localhost Parla test')
        while True:
            line = self.rfile.readline()
            if not line: return
            command = line.decode(errors='replace').strip().upper()
            if command.startswith(('EHLO', 'HELO')):
                send('250-localhost'); send('250-AUTH PLAIN LOGIN'); send('250 8BITMIME')
            elif command.startswith('AUTH PLAIN'):
                if len(command.split()) == 2: send('334 '); self.rfile.readline()
                send('235 Authentication successful')
            elif command.startswith('AUTH LOGIN'):
                send('334 VXNlcm5hbWU6'); self.rfile.readline()
                send('334 UGFzc3dvcmQ6'); self.rfile.readline(); send('235 Authentication successful')
            elif command == 'DATA':
                if reject.is_set(): send('451 Parla temporary SMTP failure'); continue
                send('354 End with dot')
                data = bytearray()
                while True:
                    line = self.rfile.readline()
                    if not line: return
                    if line == b'.\r\n': break
                    data.extend(line)
                mail.put(bytes(data)); send('250 Message accepted')
            elif command == 'QUIT': send('221 Bye'); return
            else: send('250 OK')
class IMAP(socketserver.StreamRequestHandler):
    def handle(self):
        def send(line): self.wfile.write(line.encode() + b'\r\n'); self.wfile.flush()
        send('* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN IDLE] Parla loopback')
        while True:
            line = self.rfile.readline()
            if not line: return
            parts = line.decode(errors='replace').strip().split(' ',2)
            if len(parts) < 2: continue
            tag, command = parts[:2]; command = command.upper()
            args = parts[2] if len(parts) > 2 else ''
            if command == 'CAPABILITY': send('* CAPABILITY IMAP4rev1 AUTH=PLAIN IDLE')
            elif command == 'AUTHENTICATE': send('+'); self.rfile.readline()
            elif command in ('SELECT','EXAMINE'):
                send('* FLAGS (\\Seen)'); send('* 0 EXISTS'); send('* OK [UIDVALIDITY 1] stable'); send('* OK [UIDNEXT 1] next')
            elif command in ('LIST','LSUB'): send(f'* {command} (\\HasNoChildren) "/" "INBOX"')
            elif command == 'STATUS': send('* STATUS "INBOX" (MESSAGES 0 UIDNEXT 1 UIDVALIDITY 1 UNSEEN 0)')
            elif command == 'UID' and args.upper().startswith('SEARCH'): send('* SEARCH')
            elif command == 'IDLE': send('+ idling'); self.rfile.readline()
            elif command == 'LOGOUT': send('* BYE'); send(tag + ' OK logout'); return
            elif command not in ('LOGIN','NOOP','CLOSE','UID','CREATE','SUBSCRIBE'):
                send(tag + ' BAD unsupported'); continue
            send(tag + ' OK completed')
class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

def main():
    with tempfile.TemporaryDirectory(prefix='parla-network-') as directory, Server(('127.0.0.1',0),SMTP) as smtp, Server(('127.0.0.1',0),IMAP) as imap:
        for listener in (smtp,imap): threading.Thread(target=listener.serve_forever,daemon=True).start()
        stderr = open(os.path.join(directory,'stderr.log'),'w+')
        core = subprocess.Popen([os.path.abspath(sys.argv[1])],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=stderr,text=True,
            env={**os.environ,'DC_ACCOUNTS_PATH':directory})
        replies = queue.Queue()
        def reader():
            for line in core.stdout: replies.put(json.loads(line))
        threading.Thread(target=reader,daemon=True).start()
        sequence = 0
        def call(method,*params):
            nonlocal sequence
            sequence += 1
            core.stdin.write(json.dumps(dict(jsonrpc='2.0',id=sequence,method=method,params=params))+'\n'); core.stdin.flush()
            reply = replies.get(timeout=40)
            assert reply['id'] == sequence and 'error' not in reply,(method,reply)
            return reply.get('result')
        try:
            account = call('add_account')
            call('add_or_update_transport',account,dict(addr='parla@example.invalid',password='loopback-only',
                imapServer='127.0.0.1',imapPort=imap.server_address[1],imapSecurity='plain',imapUser='parla',
                smtpServer='127.0.0.1',smtpPort=smtp.server_address[1],smtpSecurity='plain',smtpUser='parla'))
            assert call('is_configured',account)
            call('start_io',account)
            contact = call('create_contact',account,'recipient@example.invalid','Recipient')
            chat = call('create_chat_by_contact_id',account,contact)
            for number in range(3):
                message = call('send_msg',account,chat,dict(text=f'Parla SMTP test {number}'))
                received = mail.get(timeout=30)
                assert b'message-id:' in received.lower()
                deadline = time.monotonic()+20
                while call('get_message',account,message)['state'] < 26:
                    assert time.monotonic()<deadline, 'SMTP accepted but delivery state never advanced'
                    time.sleep(.05)
                assert message in call('get_message_ids',account,chat,False,False)
                assert 'Outgoing' in call('get_connectivity_html',account)
                call('stop_io',account); call('start_io',account)
            # A transient SMTP failure must stay queued, be visible in connectivity,
            # and recover on a network hint without resubmitting send_msg.
            reject.set()
            message = call('send_msg',account,chat,dict(text='Transient failure recovery'))
            deadline = time.monotonic()+20
            while 'Parla temporary SMTP failure' not in call('get_connectivity_html',account):
                assert time.monotonic()<deadline, 'SMTP failure missing from connectivity report'
                time.sleep(.05)
            assert call('get_message',account,message)['state'] < 26
            reject.clear(); call('maybe_network')
            mail.get(timeout=35)
            deadline = time.monotonic()+20
            while call('get_message',account,message)['state'] < 26:
                assert time.monotonic()<deadline, 'Queued message did not recover'
                time.sleep(.05)
            # Deleting an active scheduler must release it and leave other profiles usable.
            other = call('add_account')
            call('remove_account',account)
            call('select_account',other)
            assert [a['id'] for a in call('get_all_accounts')] == [other]
            call('set_config',other,'displayname','Still responsive')
            assert call('get_config',other,'displayname') == 'Still responsive'
            print('core network: SMTP DATA + delivered state, IMAP, SMTP failure/recovery, 3 stop/start cycles, active deletion and remaining profile OK')
        except BaseException:
            stderr.flush(); stderr.seek(0); print(stderr.read()[-12000:],file=sys.stderr); raise
        finally:
            core.stdin.close()
            try: core.wait(timeout=5)
            except subprocess.TimeoutExpired: core.kill(); core.wait()
            stderr.close()
            smtp.shutdown(); imap.shutdown()
if __name__ == '__main__': main()
