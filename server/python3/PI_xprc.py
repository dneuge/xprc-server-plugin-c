from xprc.Server import Server
from xprc.SharedResources import SharedResources

class PythonInterface:
    def __init__(self):
        self.Name = "Remote Control"
        self.Sig = "de.energiequant.xprc"
        self.Desc = "remotely control X-Plane via TCP"
        self.shared_resources = SharedResources()

    def XPluginStart(self):
        return self.Name, self.Sig, self.Desc

    def XPluginStop(self):
        self.XPluginDisable()

    def XPluginEnable(self):
        self.server_thread = Server(self.shared_resources, 'localhost')
        self.server_thread.start()
        return 1

    def XPluginDisable(self):
        self.server_thread.stop()

    def XPluginReceiveMessage(self, inFromWho, inMessage, inParam):
        pass
