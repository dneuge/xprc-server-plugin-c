class CommandCallback:
    def __init__(self, client_session, channel_id, shared_resources):
        self.shared_resources = shared_resources
        self.client_session = client_session
        self.channel_id = channel_id
        self.channel_open = True
        self.first_message = True

    def fatal_error(self, msg='', timestamp=-1):
        if timestamp < 0:
            timestamp = self.client_session.get_timestamp()
        
        if not self.channel_open:
            raise RuntimeError('Channel %s is no longer open, unable to submit more messages. Timestamp %d, message: %s' % (self.channel_id, timestamp, msg))
        
        # fatal means we cannot continue, i.e. command should terminate on its own and channel should be closed
        self.channel_open = False
        
        if msg.strip() != '':
            self.client_session.send_line('-ERR %s %d %s' % (self.channel_id, timestamp, msg))
        else:
            self.client_session.send_line('-ERR %s %d' % (self.channel_id, timestamp))
        
    def send(self, msg, timestamp=-1, close_channel=False):
        if timestamp < 0:
            timestamp = self.client_session.get_timestamp()
        
        if not self.channel_open:
            raise RuntimeError('Channel %s is no longer open, unable to submit more messages. Timestamp %d, message: %s' % (self.channel_id, timestamp, msg))
        
        channel_status = '+'
        if close_channel:
            self.channel_open = False
            channel_status = '-'
        
        if self.first_message:
            self.first_message = False
            
            if msg.strip() != '':
                self.client_session.send_line('%sACK %s %d %s' % (channel_status, self.channel_id, timestamp, msg))
            else:
                self.client_session.send_line('%sACK %s %d' % (channel_status, self.channel_id, timestamp))
        else:
            if msg.strip() != '':
                self.client_session.send_line('%s%s %d %s' % (channel_status, self.channel_id, timestamp, msg))
            else:
                self.client_session.send_line('%s%s %d' % (channel_status, self.channel_id, timestamp))

    def find_data_ref(self, name):
        return self.shared_resources.get_cached_data_ref_lookup().find(name)
    
    def command_terminated(self):
        if self.channel_open:
            print('XPRC channel %s command signals termination but channel is still open; closing as erroneous' % (self.channel_id))
            self.fatal_error('bug: command left channel open when it signalled termination')
            
        self.client_session.remove_channel(self.channel_id)
