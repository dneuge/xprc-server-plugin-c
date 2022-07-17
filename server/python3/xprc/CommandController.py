class CommandController:
    def registered(self):
        # anything requiring channel registry lookups must only be done after registration; this is the first call after __init__ when it is safe to do so
        pass
    
    def tick(self, session_timestamp):
        # periodic ticks issued by session thread
        pass
    
    def terminate(self):
        pass
