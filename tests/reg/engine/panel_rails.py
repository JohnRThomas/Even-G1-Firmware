# Loaded into the Renode monitor by driver.Session ("i @panel_rails.py"): one command for what the driver reads
# after every step, so that it does not need a dozen monitor round trips (they took about a third of a run's
# time). The panel's supplies on the G1 board (the firmware's power_up_panel / power_down_panel, src/main.c and
# src/button/button.c): the nPM1300's BUCK2 (1.2 V), its load switch LDSW1 (1.8 V, which the light sensor
# shares) and P0.24, which switches the -2 V.
from Antmicro.Renode.Core import EmulationManager


def mc_reg_panel_rails():
    """Print "panel-rails <master> <slave> <seconds>": each lens's BUCK2, LDSW1 and P0.24 as three digits
    (1 = on; "---": no such lens), and the emulation's elapsed virtual time."""
    emulation = EmulationManager.Instance.CurrentEmulation
    out = []
    for name in ("g1", "g1_slave"):
        ok, machine = emulation.TryGetMachineByName(name)
        if not ok:
            out.append("---")
            continue
        pmic = machine["sysbus.twim1.npm1300"]
        p0 = machine.SystemBus.ReadDoubleWord(0x50842504)                     # P0 OUT
        out.append("%d%d%d" % (1 if pmic.BuckEnabled(2) else 0, 1 if pmic.LdswEnabled(1) else 0, (p0 >> 24) & 1))
    print("panel-rails %s %.6f" % (" ".join(out), emulation.MasterTimeSource.ElapsedVirtualTime.TotalSeconds))
