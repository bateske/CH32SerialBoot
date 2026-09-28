"""Host-side model of micros() on the CH32X035 Arduino core.

Cycle-level model of the QingKe V4 SysTick as systick_init() sets it up
(up-counter 0..CMP, auto-reload, CNTIF set at the reload, SysTick_Handler
advances msTick), driving line-by-line ports of the old and new
getCurrentMicros() / SysTick_Handler from cores/arduino/ch32/clock.c.

It is a model of the C, not the C itself: the real check is
test/sketches/MicrosMonotonic on hardware. What this shows is *why* each
piece of the fix is there:

  formula=old handler=old   the STM32-derived (CMP + 1 - CNT) term: every
                            sample is wrong and micros() steps backwards
                            inside each millisecond.
  formula=new handler=old   correct in thread mode and with IRQs off, but an
                            ISR that outranks SysTick and lands between the
                            handler's msTick++ and SR clear counts a tick
                            twice (scenario C).
  formula=new handler=new   what clock.c now does: exact everywhere.

Run:  python test/native/sim_micros.py
Pass: the formula=new handler=new column reads 0/0/0 on every row, for all
three CNTIF timing variants (columns are backwards steps / wrong samples /
worst error in us / sample count)."""
import random, itertools

class Sim:
    def __init__(self, formula, handler, cmp_=47999, flag_lead=0, latency=6, seed=1):
        self.cmp, self.tms = cmp_, cmp_ + 1
        self.formula, self.handler, self.flag_lead = formula, handler, flag_lead
        self.cnt = self.flag = self.ms = self.cycle = 0
        self.irq, self.blocked = True, False     # MIE; reader outranks SysTick
        self.pending, self.isr_step, self.latency = None, None, latency
        self.rng = random.Random(seed)
        self.lost = 0                            # ticks the handler could never count

    # ---- hardware -----------------------------------------------------------
    def hw(self):
        self.cycle += 1
        if self.cnt == self.cmp:                 # reload edge
            self.cnt, self.flag = 0, 1
        else:
            self.cnt += 1
            if self.flag_lead and self.cnt >= self.cmp - self.flag_lead + 1:
                self.flag = 1                    # flag visible before the reload

    # ---- SysTick_Handler, one step per cycle --------------------------------
    def isr(self):
        s = self.isr_step
        if self.handler == "new":                # csrrci; SR=0; msTick++; csrw  (atomic)
            if s == 0: self.flag = 0; self.ms += 1
        else:                                    # old: msTick++; osSystickHandler(); SR=0
            if s == 0: self.ms += 1
            if s == 2: self.flag = 0
        self.isr_step = s + 1
        if self.isr_step == 4: self.isr_step = None    # mret

    def cycle_once(self):
        """One clock. Returns True if the reader got to execute this cycle."""
        self.hw()
        if self.isr_step is not None and not self.blocked:
            self.isr(); return False
        if self.flag and self.irq and not self.blocked and self.isr_step is None:
            if self.pending is None: self.pending = self.latency
            elif self.pending == 0: self.pending = None; self.isr_step = 0; return False
            else: self.pending -= 1
        else:
            self.pending = None
        return True

    def adv(self, n):
        """Advance until the reader has executed n cycles (stalled while the ISR runs)."""
        while n > 0:
            if self.cycle_once(): n -= 1

    def run_free(self, n):                       # no reader active
        while n > 0:
            if not self.flag and self.isr_step is None and self.pending is None:
                k = min(n, self.cmp - self.cnt)  # nothing can happen before the wrap
                if k > 0:
                    self.cnt += k; self.cycle += k; n -= k; continue
            self.cycle_once(); n -= 1

    def until_isr_step(self, s):                 # run until the handler is about to do step s
        for _ in range(4 * self.tms):
            if self.isr_step == s: return
            self.cycle_once()
        raise RuntimeError(f"handler never reached step {s}")

    # ---- reads with realistic instruction spacing ---------------------------
    def rd(self, what):
        self.adv(self.rng.randint(4, 9) if what == "ms" else self.rng.randint(1, 3))
        if what == "cnt": self.truth = (self.cycle * 1000) // self.tms
        return {"ms": self.ms, "cnt": self.cnt, "sr": self.flag, "cmp": self.cmp}[what]

    # ---- getCurrentMicros ----------------------------------------------------
    def micros(self):
        if self.formula == "old":
            m0 = self.rd("ms"); u0 = self.rd("cnt"); m1 = self.rd("ms"); u1 = self.rd("cnt")
            tms = self.rd("cmp") + 1
            if m1 != m0: return m1 * 1000 + ((tms - u1) * 1000) // tms
            return m0 * 1000 + ((tms - u0) * 1000) // tms
        tms = self.rd("cmp") + 1
        while True:
            m = self.rd("ms"); sr0 = self.rd("sr") & 1; u = self.rd("cnt"); sr1 = self.rd("sr") & 1
            if m == self.rd("ms") and sr0 == sr1: break
        if sr1 and u < tms - 1: m += 1
        return m * 1000 + (u * 1000) // tms

def scenarios(formula, handler, flag_lead):
    """Yield (name, list of (value, truth)) for each scenario."""
    tms = 48000
    # A: thread mode, interrupts on; dense sampling around wraps, sparse elsewhere.
    s = Sim(formula, handler, flag_lead=flag_lead); out = []
    s.run_free(tms // 2)
    for k in range(6):
        for _ in range(0, 400): out.append((s.micros(), s.truth))          # ~ms of back-to-back calls
        s.run_free(tms - 1200 - 60)                                        # land ~60 cycles before wrap
        for _ in range(0, 120): out.append((s.micros(), s.truth))          # straddle the wrap
    yield "A thread mode, IRQ on", out
    # B: interrupts disabled for windows shorter than 1 ms, straddling wraps.
    for D in (40, 300, 14000, 43000, tms - 60):
        s = Sim(formula, handler, flag_lead=flag_lead); out = []
        for lead in (5, 20, 200, 3000, D // 2, D - 5):
            s.run_free((tms - (s.cycle % tms)) - lead - 30)               # window starts `lead` before wrap
            s.irq = False; deadline = s.cycle + D
            while s.cycle < deadline - 40: out.append((s.micros(), s.truth))
            s.run_free(max(1, deadline - s.cycle)); s.irq = True
            for _ in range(20): out.append((s.micros(), s.truth))
            s.run_free(tms // 3)
        yield f"B IRQ off {D:>5} cycles", out
    # C: reader inside an ISR that outranks SysTick, incl. preempting the handler mid-way.
    s = Sim(formula, handler, flag_lead=flag_lead); out = []
    for lead in (3, 10, 40, 400, 5000):
        s.run_free((tms - (s.cycle % tms)) - lead - 30)
        s.blocked = True
        for _ in range(12): out.append((s.micros(), s.truth))
        s.blocked = False; s.run_free(tms // 4)
    for step in range(0, 4):
        for _ in range(3):
            s.until_isr_step(step); s.blocked = True
            for _ in range(4): out.append((s.micros(), s.truth))
            s.blocked = False; s.run_free(tms // 3 + 7)
    yield "C reader in higher-prio ISR", out

def evaluate(out):
    back = sum(1 for (a, _), (b, _) in zip(out, out[1:]) if b < a)
    wrong = sum(1 for v, t in out if v != t)
    worst = max((abs(v - t) for v, t in out), default=0)
    return back, wrong, worst, len(out)

variants = [("old", "old"), ("new", "old"), ("new", "new")]
for flag_lead in (0, 1, 2):
    print(f"\n=== CNTIF visible {flag_lead} cycle(s) before the reload ===")
    print(f"{'scenario':32} " + "  ".join(f"{'formula='+f+' handler='+h:>28}" for f, h in variants))
    print(f"{'':32} " + "  ".join(f"{'back/wrong/worst(us)/n':>28}" for _ in variants))
    names = None
    results = {v: dict(scenarios(v[0], v[1], flag_lead)) for v in variants}
    for name in results[variants[0]]:
        row = []
        for v in variants:
            b, w, worst, n = evaluate(results[v][name])
            row.append(f"{b:>6}/{w:>5}/{worst:>6}/{n:<5}")
        print(f"{name:32} " + "  ".join(f"{r:>28}" for r in row))
