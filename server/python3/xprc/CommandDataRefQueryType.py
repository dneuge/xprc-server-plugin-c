import xp

from .CommandController import CommandController

class CommandDataRefQueryType(CommandController):
    def __init__(self, command_params, callback):
        self.callback = callback
        self.data_ref_name = command_params.strip()
        
        if len(self.data_ref_name) == 0:
            self.data_ref_name = None
    
    def registered(self):
        self.process()
        self.callback.command_terminated()
    
    def process(self):
        if self.data_ref_name is None:
            self.callback.fatal_error('no DataRef name has been provided')
            return
        
        data_ref = self.callback.find_data_ref(self.data_ref_name)
        
        if data_ref is None:
            self.callback.fatal_error('DataRef not found: %s' % (self.data_ref_name))
            return
        
        types = self.type_flags_to_strings(xp.getDataRefTypes(data_ref))
        
        if len(types) == 0:
            self.callback.fatal_error('DataRef currently does not provide any supported types: %s' % (self.data_ref_name))
            return
        
        self.callback.send(','.join(types), close_channel=True)

    def type_flags_to_strings(self, types):
        # FIXME: use plugin-wide constants for type strings
        
        out = []
        
        if types & xp.Type_Int:
            out.append('int')

        if types & xp.Type_Float:
            out.append('float')

        if types & xp.Type_Double:
            out.append('double')

        if types & xp.Type_IntArray:
            out.append('int[]')

        if types & xp.Type_FloatArray:
            out.append('float[]')

        if types & xp.Type_Data:
            out.append('blob')

        return out
