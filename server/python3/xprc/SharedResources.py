from threading import Lock

import xp

class CachedDataRefLookup:
    def __init__(self):
        self.lock = Lock()
        self.data_ref_by_name = {}
    
    def find(self, name):
        # FIXME: check for valid name syntax/length before calling X-Plane
        
        with self.lock:
            data_ref = self.data_ref_by_name.get(name)
            if data_ref is not None:
                return data_ref
            
            data_ref = xp.findDataRef(name)
            if data_ref is not None:
                self.data_ref_by_name[name] = data_ref
            
            return data_ref

    def reset(self):
        with self.lock:
            self.data_ref_by_name = {}

class SharedResources:
    def __init__(self):
        self.cached_data_ref_lookup = CachedDataRefLookup()

    def get_cached_data_ref_lookup(self):
        return self.cached_data_ref_lookup
