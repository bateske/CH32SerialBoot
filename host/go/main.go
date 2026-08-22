// Command chgame-upload is the CHGame board uploader.
//
// It owns the ENTIRE app -> bootloader -> app transition: find the port, do the
// 1200-baud touch, wait for the bootloader, erase, write, verify, launch, and
// confirm the sketch came back. Arduino's own 1200bps touch and port-waiting are
// disabled in boards.txt precisely so there is only one actor on the port -
// two racing for it is the classic source of flaky native-USB uploads.
//
// A single static binary with no runtime dependency, shipped as an Arduino tool
// dependency. That is what the ecosystem does (avrdude, bossac, openocd) and it
// is why this is Go rather than the Python prototype it replaces: Arduino ships
// no Python interpreter, and freezing one would need a build machine per OS.
package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
	"time"
)

var version = "0.1.0"

type opts struct {
	port    string
	timeout time.Duration
	debug   bool
	verbose bool
	quiet   bool
}

func die(format string, a ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", a...)
	os.Exit(1)
}

func resolvePort(o *opts) string {
	if o.port != "" {
		return o.port
	}
	ports, err := findPorts()
	if err != nil {
		die("could not enumerate serial ports: %v", err)
	}
	switch len(ports) {
	case 0:
		die("no CHGame device found (looked for %s:%s). Is it plugged in?", chgameVID, chgamePID)
	case 1:
		return ports[0]
	default:
		die("several CHGame devices found: %s - pass -port", strings.Join(ports, ", "))
	}
	return ""
}

func progressBar(quiet bool) func(done, total int) {
	if quiet {
		return nil
	}
	const width = 32
	return func(done, total int) {
		filled, pct := 0, 100
		if total > 0 {
			filled = width * done / total
			pct = 100 * done / total
		}
		fmt.Printf("\r  [%s%s] %3d%%  %d/%d B",
			strings.Repeat("#", filled), strings.Repeat(".", width-filled),
			pct, done, total)
		if done >= total {
			fmt.Println()
		}
	}
}

func main() {
	o := &opts{}
	bind := func(fs *flag.FlagSet) {
		fs.StringVar(&o.port, "port", "", "serial port (auto-detected if omitted)")
		fs.DurationVar(&o.timeout, "timeout", 10*time.Second, "per-command timeout")
		fs.BoolVar(&o.debug, "debug", false, "dump frames in both directions")
		// Arduino's platform.txt expands {upload.verbose} to one of these.
		fs.BoolVar(&o.verbose, "verbose", false, "more detail")
		fs.BoolVar(&o.quiet, "quiet", false, "less detail")
	}

	global := flag.NewFlagSet("chgame-upload", flag.ExitOnError)
	bind(global)
	global.Usage = func() {
		fmt.Fprintf(os.Stderr, `chgame-upload %s

  chgame-upload [flags] probe
  chgame-upload [flags] info
  chgame-upload [flags] touch
  chgame-upload [flags] run
  chgame-upload [flags] flash <file.bin> [-verify] [-run]
  chgame-upload [flags] provision -bootloader <boot.bin> [-app <app.bin>] [-wchisp <path>]
  chgame-upload [flags] noop

Flags may appear before or after the subcommand.
`, version)
		global.PrintDefaults()
	}

	if err := global.Parse(os.Args[1:]); err != nil {
		os.Exit(1)
	}
	rest := global.Args()
	if len(rest) == 0 {
		global.Usage()
		os.Exit(1)
	}
	cmd := rest[0]
	rest = rest[1:]

	sub := flag.NewFlagSet(cmd, flag.ExitOnError)
	bind(sub)
	var (
		flagVerify = sub.Bool("verify", false, "read back and compare, independently of the device CRC")
		flagRun    = sub.Bool("run", false, "launch the application afterwards")
		bootFile   = sub.String("bootloader", "", "bootloader image (provision)")
		appFile    = sub.String("app", "", "application image (provision)")
		wchispPath = sub.String("wchisp", "", "path to wchisp (provision)")
	)

	// Go's flag package stops parsing at the first non-flag argument, so a plain
	// sub.Parse would silently ignore the trailing flags in
	//     flash firmware.bin -run
	// which is exactly the form platform.txt uses. Parse in a loop instead,
	// peeling off one positional at a time, so flags work on either side.
	var positional []string
	for {
		if err := sub.Parse(rest); err != nil {
			os.Exit(1)
		}
		rest = sub.Args()
		if len(rest) == 0 {
			break
		}
		positional = append(positional, rest[0])
		rest = rest[1:]
	}

	switch cmd {
	case "probe":
		doProbe(o)
	case "info":
		doInfo(o)
	case "touch":
		doTouch(o)
	case "run":
		doRun(o)
	case "flash":
		if len(positional) < 1 {
			die("flash needs an image path")
		}
		doFlash(o, positional[0], *flagVerify, *flagRun)
	case "provision":
		if *bootFile == "" {
			die("provision needs -bootloader")
		}
		if err := provision(*bootFile, *appFile, *wchispPath); err != nil {
			die("%v", err)
		}
	case "noop":
		// Arduino runs a separate chip-erase step before Burn Bootloader. The
		// erase itself is done inside provision(), so that EVERY provisioning
		// path gets it - including Upload Using Programmer, which Arduino does
		// not precede with an erase step at all.
		fmt.Println("erase: performed by the provisioning step itself")
	default:
		die("unknown command %q", cmd)
	}
}

func doProbe(o *opts) {
	ports, err := findPorts()
	if err != nil {
		die("could not enumerate serial ports: %v", err)
	}
	if len(ports) == 0 {
		fmt.Printf("no CHGame device found (%s:%s)\n", chgameVID, chgamePID)
		os.Exit(1)
	}
	for _, name := range ports {
		// quickHello, never a bare hello(): writing to a sketch that does not
		// read Serial blocks forever, and probe has to survive being pointed at
		// one.
		h, err := quickHello(name, 2*time.Second, o.debug)
		if err != nil {
			// Silence is how an application identifies itself. Sketches do not
			// implement the bootloader protocol, and must not.
			fmt.Printf("%s: application (did not answer HELLO)\n", name)
			continue
		}
		fmt.Printf("%s: %s, protocol v%d, bootloader v%d\n",
			name, h.modeName(), h.ProtoVersion, h.BootVersion)
	}
}

func doInfo(o *opts) {
	name := resolvePort(o)
	h, err := quickHello(name, o.timeout, o.debug)
	if err != nil {
		die("no response from %s: %v\n"+
			"(a running sketch does not answer this - use 'touch' first)", name, err)
	}
	fmt.Printf("port          : %s\n%s\n", name, h.describe())
}

func doTouch(o *opts) {
	name := resolvePort(o)
	if err := uploadTouch(name); err != nil {
		die("1200-baud touch failed: %v", err)
	}
	boot, err := waitForBootloader(o.timeout, name, o.debug)
	if err != nil {
		die("%v", err)
	}
	if boot == name {
		fmt.Printf("bootloader is up on %s\n", boot)
	} else {
		fmt.Printf("bootloader is up on %s (was %s)\n", boot, name)
	}
}

func doRun(o *opts) {
	name := resolvePort(o)
	h, err := quickHello(name, o.timeout, o.debug)
	if err != nil {
		die("no response from %s: %v", name, err)
	}
	if h.Mode != modeBootloader {
		die("%s is in %s mode, not the bootloader", name, h.modeName())
	}
	if h.AppState != 0 {
		die("bootloader reports the application image is not valid (%s); refusing to RUN",
			appStateNames[h.AppState])
	}
	c, err := openClient(name, o.timeout, o.debug)
	if err != nil {
		die("could not open %s: %v", name, err)
	}
	// The device vanishes as it launches, so a lost acknowledgement is expected.
	_, _ = c.request(cmdRun, nil, 2*time.Second)
	c.Close()
	fmt.Println("sent RUN - device should now be running the application")
}

func doFlash(o *opts, image string, verify, run bool) {
	data, err := os.ReadFile(image)
	if err != nil {
		die("cannot read %s: %v", image, err)
	}
	name := resolvePort(o)

	timeout := o.timeout
	if timeout < 10*time.Second {
		timeout = 10 * time.Second
	}

	bootPort, err := ensureBootloader(name, timeout, o.debug)
	if err != nil {
		die("could not put %s into the bootloader: %v", name, err)
	}
	if bootPort != name {
		fmt.Printf("note    : bootloader appeared on %s (was %s)\n", bootPort, name)
	}

	c, err := openClient(bootPort, timeout, o.debug)
	if err != nil {
		die("could not open %s: %v", bootPort, err)
	}
	defer c.Close()

	fmt.Printf("port    : %s\n", bootPort)
	fmt.Printf("image   : %s  (%d bytes)\n", image, len(data))

	h, err := c.hello()
	if err != nil {
		die("no response from %s: %v", bootPort, err)
	}
	fmt.Printf("region  : 0x%04X + %d bytes\n", h.AppStart, h.AppMaxSize)

	res, err := upload(c, data, progressBar(o.quiet), verify)
	if err != nil {
		die("upload failed: %v", err)
	}

	fmt.Printf("crc32   : 0x%08X\n", res.CRC32)
	fmt.Printf("erase   : %.2f s\n", res.EraseS)
	fmt.Printf("write   : %.2f s  (%.1f KiB/s, %d B chunks)\n", res.WriteS, res.KiBps, res.Chunk)
	if verify {
		fmt.Println("readback: MATCHES")
	}
	fmt.Printf("total   : %.2f s  -- image accepted and marked valid\n", res.TotalS)

	if run {
		_, _ = c.request(cmdRun, nil, 2*time.Second)
		c.Close()
		fmt.Println("sent RUN")
		if back, err := waitForApplication(6*time.Second, o.debug); err == nil {
			fmt.Printf("running : application is up on %s\n", back)
		} else {
			fmt.Println("warning : the application did not re-enumerate within 6s")
		}
	}
}
