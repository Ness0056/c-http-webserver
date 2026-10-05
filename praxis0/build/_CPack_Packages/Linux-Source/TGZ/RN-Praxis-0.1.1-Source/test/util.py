import subprocess
import time


class KillOnExit(subprocess.Popen):
    """A Popen subclass that kills the subprocess when its context exits"""

    def __init__(self, *args, **kwargs):
        super().__init__(
            *args, **kwargs,
            # stdout=subprocess.PIPE,
        )
        time.sleep(.1)

    def __exit__(self, exc_type, value, traceback):
        self.kill()

        super().__exit__(exc_type, value, traceback)
        time.sleep(.1)
        
        
# check if current system is Ubuntu 20 EECS test system
def is_ubuntu20_eecs():
    required_operating_system = "Operating System: Ubuntu 20.04.6 LTS"
    required_domain_name = "eecsit.tu-berlin.de"

    hostnamectl = subprocess.Popen("hostnamectl", stdout=subprocess.PIPE)
    os_name = subprocess.check_output(["grep", "-o", "Operating System:.*"], stdin=hostnamectl.stdout).decode("utf-8").strip()
    hostnamectl.wait()

    domain_name = subprocess.check_output(["hostname", "-d"]).decode("utf-8").strip()

    return required_operating_system == os_name and required_domain_name == domain_name
