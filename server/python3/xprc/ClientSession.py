from datetime import datetime, timedelta, timezone
from random import randrange
import re
import socket
from threading import Lock, Thread
from time import sleep

re_command = re.compile('^([A-Za-z0-9]{4}) ([A-Z]{4})((?:;[a-z0-9]+=[^;]+)*)(| (.+))$')
def parse_command(line):
    m = re_command.match(line)
    if m is None:
        return None
    
    channel_id = m.group(1)
    command_name = m.group(2)
    command_options = {pair[:pair.find('=')]: pair[pair.find('=')+1:] for pair in m.group(3).split(';') if pair != ''}
    command_params = m.group(5)
    
    return (channel_id, command_name, command_options, command_params)

class ClientSession(Thread):
    def __init__(self, socket, server):
        super().__init__()
        self.socket = socket
        self.server = server
        self.shutdown = False
        self.handshake_complete = False
        self.handshake_data = {}
        self.session_start_time = None
        self.commands_by_channel = {}
        self.send_lock = Lock()
    
    def run(self):
        # TODO: set timeout for handshake
        print('client thread')
        
        self.send_line('XPRC;version,password')
        
        message_buffer = bytearray()
        message_buffer_insert_position = 0
        
        receive_buffer = bytearray(4096)
        while not self.shutdown:
            try:
                num_received = self.socket.recv_into(receive_buffer)
                message_buffer[message_buffer_insert_position:] = receive_buffer[:num_received]
                message_buffer_insert_position = message_buffer_insert_position + num_received
                
                message_end_position = message_buffer.find(b'\n')
                while message_end_position >= 0:
                    line = str(message_buffer[:message_end_position], encoding='us-ascii').strip('\r\n')
                    self.on_line_received(line)
                    message_buffer = message_buffer[message_end_position+1:]
                    message_buffer_insert_position = message_buffer_insert_position - message_end_position - 1
                    message_end_position = message_buffer.find(b'\n')
                
                if len(message_buffer) > 1048576:
                    print('XPRC client exceeds maximum message buffer size, shutting down')
                    self.shutdown = True
            except Exception as err:
                print('XPRC client error, shutting down connection: %s' % err)
                self.shutdown = True
        
        self.stop()
        
        self.server.remove_client_session(self)

    def send_line(self, line):
        self.send_lock.acquire()
        self.socket.sendall(line.encode('us-ascii') + b'\n')
        self.send_lock.release()
    
    def on_line_received(self, line):
        if self.handshake_complete:
            self.on_command_received(line)
        else:
            if len(self.handshake_data) == 0:
                self.handshake_data['version'] = line
            elif len(self.handshake_data) == 1:
                self.handshake_data['password'] = line
                self.on_handshake_received()
    
    def on_handshake_received(self):
        print('handshake: %s' % self.handshake_data)
        is_version_ok = self.handshake_data['version'] == 'v1'
        is_password_ok = self.server.check_password(self.handshake_data['password'])
        del self.handshake_data['password']
        
        sleep(randrange(500, 2500) / float(1000.0))
        
        if not is_password_ok:
            print('XPRC client handshake failed: wrong password')
            self.stop()
        elif not is_version_ok:
            print('XPRC client handshake failed: unsupported protocol version')
            self.send_line('v1;ERR:unsupported protocol version')
            self.stop()
        else:
            self.session_start_time = datetime.now(tz=timezone.utc)
            self.send_line('v1;OK;'+self.session_start_time.isoformat())
            self.handshake_complete = True
    
    def get_timestamp(self):
        now = (datetime.now(tz=timezone.utc) - self.session_start_time)
        return round(((now.days * 86400 + now.seconds) * 1000000 + now.microseconds) / 1000)
    
    def on_command_received(self, line):
        print('command line: %s' % line)
        parsed = parse_command(line)
        
        timestamp = self.get_timestamp()
        
        if parsed is None:
            self.send_line('*ERR %d invalid syntax' % (timestamp))
            return
        
        (channel_id, command_name, command_options, command_params) = parsed
        
        if channel_id in self.commands_by_channel:
            if command_name == 'TERM':
                del self.commands_by_channel[channel_id]
                self.send_line('-ACK %s %d' % (channel_id, timestamp))
            else:
                self.send_line('+ERR %s %d channel already in use' % (channel_id, timestamp))
        else:
            if command_name == 'TERM':
                self.send_line('-ERR %s %d channel does not exist' % (channel_id, timestamp))
            else:
                self.commands_by_channel[channel_id] = {
                    'command': command_name,
                    'options': command_options,
                    'params': command_params
                }
                self.send_line('+ACK %s %d' % (channel_id, timestamp))
        
        print(self.commands_by_channel)
    
    def stop(self):
        if self.shutdown:
            print('XPRC client thread stop has already been requested, ignoring repeated call')
            return
        
        self.shutdown = True
        self.socket.shutdown(socket.SHUT_RDWR)
        self.socket.close()
