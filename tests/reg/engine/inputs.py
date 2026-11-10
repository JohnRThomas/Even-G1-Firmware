"""The shared names of the regression tests: the lenses, the prefixes of the peripheral models in monitor
commands, and the inputs that are not written by a phone (those are its protocol's: a suite's suite.py).
"""
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# The lenses an assert or an input names: the master, the slave, both (an assert must hold on each), and, in
# an assert only, the lens of the window's anchor.
M, S, B, A = "master", "slave", "both", "anchor"

IMU, TOUCH, NPM, OPT, NFC, JBD, PDM = (
    "sysbus.twim2.lsm6dso ", "sysbus.twim2.touch ", "sysbus.twim1.npm1300 ", "sysbus.twim1.opt3001 ",
    "sysbus.twim1.st25dv ", "spim_a.jbd ", "sysbus.pdm0 ")

# accelerometer poses for pose(): the head level, raised by 45, 70 and 81 degrees, and the glasses lying flat
LEVEL, UP45, UP70, UP81, FLAT = ((0.342, 0.0, 0.94), (0.707, 0.0, 0.707), (0.94, 0.0, 0.342), (0.988, 0.0, 0.156),
                                 (0.0, 0.0, 1.0))

# The display model's journal (JBD + "GetJournal") numbers and times its entries, "#12 t=1.234 op=0x02 ...":
# what a check() compares is the entry without them, "02 ...".
JOURNAL_ENTRY = re.compile(r"^#\d+ t=\S+ op=0x")


def case(level, lid):
    """A message of the charging case's NFC reader (pt command 0x13), with its checksum."""
    m = bytes((0x13, 1, 2, 2, level, lid))
    return '%sRfPutMailboxMessage "%s"' % (NFC, (m + bytes((sum(m) & 255,))).hex().upper())


def query_output(lines):
    """The output lines of a monitor query as a check() compares them: the models' own decorations taken off."""
    return [JOURNAL_ENTRY.sub("", line) for line in lines]
