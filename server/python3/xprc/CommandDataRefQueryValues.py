import re
from threading import Lock

import xp

from .CommandController import CommandController

re_frequency = re.compile('^([1-9]+[0-9]*)(ms|f)$')
def parse_frequency(s):
    m = re_frequency.match(s)
    if m is None:
        return None
    
    value = int(m.group(1))
    mode = m.group(2)
    
    return (value, mode)

re_times = re.compile('^([1-9]+[0-9]*)$')
def parse_times(s):
    m = re_times.match(s)
    if m is None:
        return None
    
    return int(s)

def parse_wanted_data_ref(s):
    tmp = s.split(':', 1)
    if len(tmp) != 2:
        return None
    
    # FIXME: use a constant for all supported type names
    if tmp[0] not in ['int', 'float', 'double', 'float[]', 'double[]', 'blob']:
        return None
    
    return tmp

def format_value(value_type, value):
    # TODO: extract to helper module
    if value_type == 'int':
        return str(value)
    elif value_type == 'float':
        # TODO: this probably is not the format shown in protocol documentation
        return str(value)
    elif value_type == 'double':
        # TODO: this probably is not the format shown in protocol documentation
        return str(value)
    elif value_type == 'int[]':
        return ','.join([str(len(value))] + [format_value('int', x) for x in value])
    elif value_type == 'float[]':
        return ','.join([str(len(value))] + [format_value('float', x) for x in value])
    elif value_type == 'blob':
        return '%d,%s' % (len(value), bytearray(value).hex().upper())

class CommandDataRefQueryValues(CommandController):
    def __init__(self, options, params, callback):
        self.callback = callback
        self.options = options
        self.params = params
        self.sample_queue = []
        self.sample_lock = Lock()
        self.process_lock = Lock()
        self.sample_timer = None
        self.process_timer = None
        self.error_msg = None
        self.reached_end_of_queue = False
    
    def registered(self):
        self.frequency = parse_frequency(self.options.get('freq', '1000ms'))
        if self.frequency is None:
            print('XPRC failed to parse frequency option: "%s"' % (self.options['freq']))
            self.callback.fatal_error('could not parse frequency')
            self.command_terminated()
            return
        (frequency_value, frequency_mode) = self.frequency
        
        self.remaining_iterations = parse_times(self.options['times']) if 'times' in self.options else -1
        if self.remaining_iterations is None:
            print('XPRC failed to parse times option: "%s"' % (self.options['times']))
            self.callback.fatal_error('could not parse times option')
            self.command_terminated()
            return
            
        self.wanted_data_refs = []
        for req in self.params.split(';'):
            wanted = parse_wanted_data_ref(req)
            if wanted is None:
                print('XPRC failed to parse parameter: "%s"' % (req))
                self.callback.fatal_error('failed to parse parameter "%s"' % (req))
                self.command_terminated()
                return
            
            (wanted_type, data_ref_name) = wanted
            data_ref = self.callback.find_data_ref(data_ref_name)
            if data_ref is None:
                self.callback.fatal_error('DataRef %s not found' % (data_ref_name))
                self.command_terminated()
                return
            
            available_types = self.type_flags_to_strings(xp.getDataRefTypes(data_ref))
            if wanted_type not in available_types:
                self.callback.fatal_error('DataRef %s does not provide requested type %s' % (data_ref_name, wanted_type))
                self.command_terminated()
                return
            
            # FIXME: use type constants
            getter = None
            is_array = False
            if wanted_type == 'int':
                getter = xp.getDatai
            elif wanted_type == 'float':
                getter = xp.getDataf
            elif wanted_type == 'double':
                getter = xp.getDatad
            elif wanted_type == 'int[]':
                getter = xp.getDatavi
                is_array = True
            elif wanted_type == 'float[]':
                getter = xp.getDatavf
                is_array = True
            elif wanted_type == 'blob':
                getter = xp.getDatab
                is_array = True
            else:
                raise RuntimeError('Unhandled type: %s' % wanted_type)
            
            self.wanted_data_refs.append((wanted_type, data_ref, data_ref_name, getter, is_array))
        
        # ACK
        self.callback.send()
        
        self.process_timer = self.callback.schedule_milliseconds(200, self.process)
        
        if frequency_mode == 'f':
            # for frame-based intervals we simply wait for the next frame
            self.sample_timer = self.callback.schedule_frametick(self.sample)
        else:
            # for time-based intervals we sample all data once upon scheduling
            self.sample_timer = self.callback.schedule_milliseconds(frequency_value, self.sample)
            self.sample(self.callback.get_session_timestamp())

    def sample(self, timestamp):
        # negative iterations mean infinite repetition, otherwise count down to zero, then stop
        if self.remaining_iterations == 0:
            return
        elif self.remaining_iterations > 0:
            self.remaining_iterations -= 1
        
        values = []
        for wanted_type, data_ref, data_ref_name, getter, is_array in self.wanted_data_refs:
            if not is_array:
                value = getter(data_ref)
            else:
                value = []
                length = getter(data_ref, value)
                if len(value) != length:
                    # termination is left up to processing thread as this may get called during frame callback
                    self.remaining_iterations = 0
                    self.error_msg = 'DataRef array query for %s:%s returned %d items but indicated a size of %d' % (wanted_type, data_ref_name, len(value), length)
                    return
            
            values.append(value)
        
        with self.sample_lock:
            self.sample_queue.append((timestamp, values, (self.remaining_iterations == 0)))
    
    def process(self, _callback_timestamp=0, terminating=False):
        with self.process_lock:
            queue = []
            with self.sample_lock:
                queue = self.sample_queue
                self.sample_queue = []
            
            for sample_timestamp, values, end_of_queue in queue:
                msg = ';'.join([format_value(wanted_type, value) for value, (wanted_type, _a, _b, _c, _d) in zip(values, self.wanted_data_refs)])
                self.callback.send(msg, timestamp=sample_timestamp, close_channel=end_of_queue)
            
                if end_of_queue:
                    self.reached_end_of_queue = True
                    break
        
        if self.reached_end_of_queue and not terminating:
            self.terminate()
    
    def terminate(self):
        external_termination = (self.remaining_iterations != 0)
        
        self.remaining_iterations = 0
        self.callback.unschedule(self.sample_timer)
        self.callback.unschedule(self.process_timer)

        should_flush = False
        with self.process_lock:
            if not self.reached_end_of_queue:
                with self.sample_lock:
                    should_flush = (len(self.sample_queue) > 0)
        
        if should_flush:
            self.process(terminating=True)
        
        if not external_termination and not self.reached_end_of_queue and self.error_msg is None:
            self.error_msg = 'samples are incomplete'
        
        # TODO: on external termination without any queued up samples we probably still have to signal successful termination
        
        if self.error_msg is not None:
            print('XPRC error in DRQV: %s' % (self.error_msg))
            
            # we can only send an error if channel was not closed which happens on "end of queue" sample
            if not self.reached_end_of_queue:
                self.callback.fatal_error(self.error_msg)
        
        self.callback.command_terminated()

    def type_flags_to_strings(self, types):
        # FIXME: duplicated from DRQT command, extract
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
