"""A Renode session driven step by step: the simulator set up as g1sim.py does it (its session_lines()), then
monitor commands sent over its console and the emulation advanced with RunFor between them, so a test can send
an input, watch what the glasses do, and send the next one when it has what it was waiting for.

Renode's console carries the monitor only: its log goes to OUT/log.txt (logFile, with the console's log
switched off), which the trace reads as it grows. Every command is followed by an echo marker; the driver reads
the console until the marker, so a command has completed, and its RunFor has elapsed, before the next one goes.
The console is also written to OUT/monitor.txt and the panel contents to OUT/master.frames and
OUT/slave.frames, as in a run of g1sim.py.

After every RunFor the driver reads, with one command (panel_rails.py), whether each panel has its supplies and
how far the emulation has come. The supplies: the display model records pixel writes only, so a panel switched
off without a clearing write would otherwise still show its last picture (trace.lit()). The time: the driver
counts the seconds it asked for, the items of the trace carry the emulation's own, and a run in which the two
drift apart is stopped.
"""
import atexit
import importlib.util
import os
import queue
import re
import shutil
import signal
import subprocess
import threading
import time

from .inputs import M, S, B, REPO

SIM = os.path.join(REPO, "tools", "simulator")
PANEL_RAILS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "panel_rails.py")
PROMPT = re.compile(r"^\([\w-]+\) ")            # a line of the console that echoes a command: "(g1) command"
RAILS = re.compile(r"^panel-rails ([01-]{3}) ([01-]{3}) ([\d.]+)$")
ERROR = "There was an error executing command"  # how the monitor starts its answer to a command it cannot run
DRIFT = 0.001                                   # seconds the emulation's clock may differ from the driver's
STARTING = 600                                  # seconds Renode has to answer in, while it starts and later
live = set()                                    # the sessions whose Renode runs, for the interpreter's exit


class DriverError(Exception):
    pass


class Stalled(DriverError):
    """Renode did not answer in time: the run can be tried again."""


@atexit.register
def kill_all():
    for session in list(live):
        session.kill()


class Session:
    def __init__(self, out, firmware, renode, seed, phone=False, execute=()):
        """A session to be: nothing runs until start(). phone: set the time quantum the virtual phone needs.
        execute: monitor commands to run once the lenses exist ("slave: ..." on the slave lens)."""
        self.out = out
        self.firmware = firmware
        self.renode = renode
        self.phone = phone
        self.execute = ["emulation SetSeed %d" % seed] + list(execute)
        self.now = 0.0                      # emulated seconds, as the driver counts them
        self.sent = 0                       # markers sent
        self.lit = {M: None, S: None}       # whether each panel had its supplies at the last look
        self.lines = queue.Queue()          # the console's lines, from the reader thread
        self.process = None
        self.monitor = None                 # OUT/monitor.txt
        self.reader = None
        self.g1sim = None                   # the simulator's launcher, as a module of this session
        self.killed = threading.Event()

    def start(self):
        """A fresh simulator copy under OUT/sim (new flash), the firmware loaded, the emulation paused at 0 s.
        Whatever goes wrong, no Renode is left behind."""
        try:
            self.launch()
            self.sync()                         # the setup script has run once the first marker comes back
            if self.rails() is None:
                raise DriverError("the simulator has no reg_panel_rails command (%s)" % PANEL_RAILS)
        except BaseException:
            self.kill()
            self.close()
            raise

    def launch(self):
        sim, work = os.path.join(self.out, "sim"), os.path.join(self.out, "work")
        shutil.copytree(SIM, sim, ignore=shutil.ignore_patterns(".renode", ".git", "__pycache__",
                                                                "g1_extstore_left.bin", "g1_extstore_right.bin"))
        os.makedirs(work, exist_ok=True)
        # The launcher as a private module of this session: several sessions run in one process, and each
        # needs its own copy, with its HERE in its own simulator directory.
        spec = importlib.util.spec_from_file_location("g1sim_%d" % id(self), os.path.join(sim, "g1sim.py"))
        self.g1sim = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.g1sim)
        if not hasattr(self.g1sim, "session_lines"):
            raise DriverError("tools/simulator/g1sim.py has no session_lines(): the simulator is older than the tests")
        self.monitor = open(os.path.join(self.out, "monitor.txt"), "w", encoding="utf-8")
        try:
            setup = self.g1sim.session_lines([self.firmware], work, self.execute, self.phone, self.out,
                                             lambda text: self.monitor.write(text + "\n"))
        except SystemExit as e:                 # the launcher's die()
            raise DriverError(str(e))
        log = self.g1sim.monitor_path(os.path.join(self.out, "log.txt"))
        session = os.path.join(work, "session.resc")
        with open(session, "w", encoding="utf-8") as f:
            f.write("\n".join(["logFile %s true" % log] + setup + ["i @" + PANEL_RAILS]) + "\n")
        self.monitor.flush()
        if self.killed.is_set():                # killed while it was being set up
            raise DriverError("the run was stopped")
        self.process = subprocess.Popen(
            [self.renode, "--disable-xwt", "--console", "--plain", "--hide-log", session], cwd=sim,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
            errors="replace", bufsize=1, start_new_session=True)
        live.add(self)
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        """The reader thread: Renode's console, line by line, into the queue and OUT/monitor.txt."""
        for line in self.process.stdout:
            self.monitor.write(line)
            self.lines.put(line.rstrip("\n"))
        self.lines.put(None)

    def write(self, command):
        try:
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError):
            raise DriverError("Renode is gone")

    def sync(self, timeout=STARTING):
        """Send a marker and return the console's lines up to it: the output of the commands before it."""
        self.sent += 1
        marker = "@@%d@@" % self.sent
        self.write('echo "%s"' % marker)
        got, deadline = [], time.time() + timeout
        while True:
            try:
                line = self.lines.get(timeout=max(0.0, deadline - time.time()))
            except queue.Empty:
                raise Stalled("no answer from Renode within %d s" % timeout)
            if line is None:
                raise DriverError("Renode exited: %s" % " | ".join(x for x in got[-5:] if x.strip()))
            if line.strip() == marker:
                return [x.strip() for x in got if x.strip() and not PROMPT.match(x)]
            got.append(line)

    def lens_commands(self, command, lens):
        """Monitor commands for a command on a lens (M, S or B)."""
        out = []
        for one in (M, S) if lens == B else (lens,):
            out += self.g1sim.for_lens(("slave: " if one == S else "") + command)
        return out

    def command(self, command, lens=M):
        """Run a monitor command on the lens(es); returns its output lines. A command the monitor cannot run
        is an error of the test file that sent it, not something to go on after."""
        for c in self.lens_commands(command, lens):
            self.write(c)
        out = self.sync()
        if any(line.startswith(ERROR) for line in out):
            raise DriverError("the monitor command %r on %s: %s" % (command, lens, " ".join(out)))
        return out

    def query(self, command, lens=M):
        """A monitor command whose output is wanted: [(lens, its output lines)]."""
        return [(one, self.command(command, one)) for one in ((M, S) if lens == B else (lens,))]

    def run_for(self, seconds):
        """Advance the emulation; returns the panels whose supplies changed, [(lens, on)]."""
        if seconds <= 0:
            return []
        self.write('emulation RunFor "%s"' % seconds)
        self.sync()
        self.now = round(self.now + seconds, 6)
        return self.look()

    def rails(self):
        """The reg_panel_rails command's answer (RAILS), or None when it is not there."""
        self.write("reg_panel_rails")
        return next((g for g in map(RAILS.match, self.sync()) if g), None)

    def look(self):
        """Read each panel's supplies and the emulation's clock: [(lens, on)] of the panels that changed."""
        got = self.rails()
        if got is None:
            raise DriverError("no answer to reg_panel_rails")
        if abs(float(got[3]) - self.now) > DRIFT:
            raise DriverError("the emulation is at %s s where the driver counts %.6f s: RunFor does not advance "
                              "it by what it is asked for (the step is not a multiple of the time quantum?)"
                              % (got[3], self.now))
        changed = []
        for lens, bits in zip((M, S), got.groups()):
            on = bits == "111"
            if "-" not in bits and on != self.lit[lens]:
                self.lit[lens] = on
                changed.append((lens, on))
        return changed

    def stop(self):
        """End the session in good order: the recordings closed, Renode quit, the simulator copy removed."""
        if self.process and self.process.poll() is None and not self.killed.is_set():
            try:
                for c in self.g1sim.frame_recording(self.out, "StopRecording"):
                    self.write(c)
                self.write("quit")
                self.process.wait(timeout=60)
            except (DriverError, subprocess.TimeoutExpired, OSError):
                self.kill()
        self.close()

    def kill(self):
        """Stop Renode at once; safe at any time, also before it has started and more than once."""
        self.killed.set()
        if self.process and self.process.poll() is None:
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.process.pid)], capture_output=True)
            else:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except OSError:
                    pass

    def close(self):
        """After stop() or kill(): wait for Renode and its reader, close the files, remove the simulator copy."""
        live.discard(self)
        if self.process:
            try:
                self.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                pass
        if self.reader:
            self.reader.join(timeout=30)        # it writes to monitor.txt until the console closes
        if self.monitor and not (self.reader and self.reader.is_alive()):
            self.monitor.close()
        shutil.rmtree(os.path.join(self.out, "sim"), ignore_errors=True)
        shutil.rmtree(os.path.join(self.out, "work"), ignore_errors=True)
