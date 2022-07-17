from random import choice
import socket
from threading import Thread, Lock

from .ClientSession import ClientSession

class Server(Thread):
    def __init__(self, shared_resources, host_address='', host_port=8962, enable_ipv6=True, password=None):
        super().__init__()
        self.shared_resources = shared_resources
        
        self.address = (host_address, host_port)
        self.network_family = socket.AF_INET6 if enable_ipv6 else socket.AF_INET
        self.password = password
        
        self.dualstack_ipv6 = socket.has_dualstack_ipv6()
        self.shutdown = False
        
        self.lock = Lock()
        self.client_sessions = set()
        
        if not self.is_password_acceptable(self.password):
            self.password = ''.join(choice('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./#$!') for i in range(16))
            print('XPRC auto-generated password: %s' % self.password)
    
    def set_password(self, password):
        if not is_password_accepted(password):
            return False
        
        self.password = password
        return True
    
    def is_password_acceptable(self, password):
        return password is not None and len(password) >= 16
    
    def run(self):
        print('XPRC server thread starting')
        
        # TODO: limit number of clients with unfinished handshakes
        
        try:
            self.server_socket = socket.create_server(self.address, family=self.network_family, dualstack_ipv6=self.dualstack_ipv6, reuse_port=True)
            print('XPRC server socket opened')
            while not self.shutdown:
                client_socket, client_address = self.server_socket.accept()
                print('XPRC connected from %s' % client_address[0])
                client_session = ClientSession(client_socket, self, self.shared_resources)
                client_session.start()
                self.add_client_session(client_session)
        except Exception as e:
            if not self.shutdown:
                print('XPRC error in server thread: %s' % e)
            self.shutdown = True
        
        print('XPRC closing server socket')
        self.server_socket.close()
        
        client_sessions = self.get_client_sessions()
        print('XPRC server thread stopping %d client sessions' % len(client_sessions))
        for client_session in client_sessions:
            client_session.stop()
        
        print('XPRC server thread stopped')

    def stop(self):
        if self.shutdown:
            print('XPRC server thread stop has already been requested, ignoring repeated call')
            return
        
        print('XPRC server thread stop requested')
        self.shutdown = True
        self.server_socket.shutdown(socket.SHUT_RDWR)
        self.server_socket.close()
        
        print('XPRC server thread stop request posted')

    def check_password(self, password):
        return (password == self.password)

    def add_client_session(self, client_session):
        with self.lock:
            self.client_sessions.add(client_session)

    def get_client_sessions(self):
        with self.lock:
            return set(self.client_sessions)

    def remove_client_session(self, client_session):
        with self.lock:
            self.client_sessions.remove(client_session)
