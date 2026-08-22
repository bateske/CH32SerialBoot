package main

// Firmware upload, image composition, and factory provisioning.

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// Flash layout - mirrors bootloader/src/chgame_map.h. The device reports its own
// region at HELLO and that is what an upload is bounds-checked against; these
// constants are only needed for composing a provisioning image, where there is
// no device to ask.
const (
	flashSize  = 0x0000F800 // 62 KB user code flash
	pageSize   = 256
	bootSize   = 0x00003000 // 12 KB bootloader reservation
	appStart   = bootSize
	metaAddr   = flashSize - pageSize
	appMaxSize = metaAddr - appStart

	metaMagic   = 0x4D474843 // "CHGM"
	metaVersion = 1
	erased      = 0xFF

	devKey = 0x43484744 // "CHGD" - an interlock against accidents, not a secret
)

func padToWord(b []byte) []byte {
	if n := len(b) % 4; n != 0 {
		pad := make([]byte, 4-n)
		for i := range pad {
			pad[i] = erased
		}
		return append(append([]byte{}, b...), pad...)
	}
	return b
}

// chunkSize is the largest WRITE data chunk the device will accept.
// Prefer whole flash pages so it commits one page per frame.
func chunkSize(maxPayload, page int) int {
	usable := maxPayload - 4 // 4-byte offset field
	if usable >= page {
		return (usable / page) * page
	}
	return usable
}

type uploadResult struct {
	Bytes   int
	CRC32   uint32
	Chunk   int
	EraseS  float64
	WriteS  float64
	TotalS  float64
	KiBps   float64
	Verified bool
}

func upload(c *Client, image []byte, progress func(done, total int), verify bool) (*uploadResult, error) {
	h, err := c.hello()
	if err != nil {
		return nil, err
	}
	if h.Mode != modeBootloader {
		return nil, fmt.Errorf("device is not in bootloader mode (mode=%d)", h.Mode)
	}

	image = padToWord(image)
	if uint32(len(image)) > h.AppMaxSize {
		return nil, fmt.Errorf("image is %d bytes; the application region holds %d",
			len(image), h.AppMaxSize)
	}

	sum := crc32.ChecksumIEEE(image)
	step := chunkSize(int(h.MaxPayload), int(h.PageSize))

	t0 := time.Now()
	begin := make([]byte, 8)
	binary.LittleEndian.PutUint32(begin[0:4], uint32(len(image)))
	binary.LittleEndian.PutUint32(begin[4:8], sum)
	// BEGIN erases; on a nearly full image that is ~200 page erases, so it gets
	// a longer deadline than an ordinary command.
	if _, err := c.check(cmdBegin, begin, 30*time.Second); err != nil {
		return nil, err
	}
	eraseS := time.Since(t0).Seconds()

	sent := 0
	for sent < len(image) {
		end := sent + step
		if end > len(image) {
			end = len(image)
		}
		payload := make([]byte, 4, 4+end-sent)
		binary.LittleEndian.PutUint32(payload[0:4], uint32(sent))
		payload = append(payload, image[sent:end]...)
		if _, err := c.check(cmdWrite, payload, 0); err != nil {
			return nil, err
		}
		sent = end
		if progress != nil {
			progress(sent, len(image))
		}
	}
	writeS := time.Since(t0).Seconds() - eraseS

	if _, err := c.check(cmdEnd, nil, 30*time.Second); err != nil {
		return nil, err
	}

	res := &uploadResult{
		Bytes: len(image), CRC32: sum, Chunk: step,
		EraseS: eraseS, WriteS: writeS, TotalS: time.Since(t0).Seconds(),
	}
	if writeS > 0 {
		res.KiBps = float64(len(image)) / 1024.0 / writeS
	}

	if verify {
		ok, err := readbackMatches(c, h, image)
		if err != nil {
			return res, err
		}
		if !ok {
			return res, fmt.Errorf("readback mismatch")
		}
		res.Verified = true
	}
	return res, nil
}

// readbackMatches reads the region back and compares byte for byte.
//
// END already verifies a CRC computed ON the device. This verifies through a
// different path entirely, which is what catches a bootloader that computes its
// CRC over the wrong span or reports success it did not achieve.
func readbackMatches(c *Client, h *Hello, image []byte) (bool, error) {
	step := int(h.MaxPayload) - 1 // response payload is status byte + data
	for off := 0; off < len(image); {
		n := step
		if off+n > len(image) {
			n = len(image) - off
		}
		req := make([]byte, 6)
		binary.LittleEndian.PutUint32(req[0:4], h.AppStart+uint32(off))
		binary.LittleEndian.PutUint16(req[4:6], uint16(n))
		r, err := c.check(cmdRead, req, 0)
		if err != nil {
			return false, err
		}
		if len(r) < 1+n {
			return false, fmt.Errorf("short READ reply: %d bytes", len(r))
		}
		for i := 0; i < n; i++ {
			if r[1+i] != image[off+i] {
				return false, nil
			}
		}
		off += n
	}
	return true, nil
}

// ---- provisioning image ----------------------------------------------------

func buildMetadata(app []byte) []byte {
	page := make([]byte, pageSize)
	for i := range page {
		page[i] = erased
	}
	binary.LittleEndian.PutUint32(page[0:4], metaMagic)
	binary.LittleEndian.PutUint32(page[4:8], metaVersion)
	binary.LittleEndian.PutUint32(page[8:12], uint32(len(app)))
	binary.LittleEndian.PutUint32(page[12:16], crc32.ChecksumIEEE(app))
	binary.LittleEndian.PutUint32(page[16:20], 0) // app version
	return page
}

// buildImage composes the full flash image for a factory/recovery write.
//
// With no application the device comes up in the bootloader and waits, which is
// the correct state for a freshly provisioned board: it enumerates as a CDC port
// immediately and the first sketch goes on over USB with no further use of BOOT.
func buildImage(boot, app []byte) ([]byte, error) {
	if len(boot) > bootSize {
		return nil, fmt.Errorf("bootloader is %d bytes, over the %d-byte reservation",
			len(boot), bootSize)
	}
	img := make([]byte, flashSize)
	for i := range img {
		img[i] = erased
	}
	copy(img, boot)

	if app != nil {
		app = padToWord(app)
		if len(app) > appMaxSize {
			return nil, fmt.Errorf("application is %d bytes, over the %d-byte region",
				len(app), appMaxSize)
		}
		copy(img[appStart:], app)
		copy(img[metaAddr:], buildMetadata(app))
	}
	return trimErased(img), nil
}

func trimErased(img []byte) []byte {
	end := len(img)
	for end > 0 && img[end-1] == erased {
		end--
	}
	return img[:end]
}

// ---- wchisp ----------------------------------------------------------------

func findWchisp(explicit string) (string, error) {
	var candidates []string
	if explicit != "" {
		candidates = append(candidates, explicit)
	}
	if env := os.Getenv("CHGAME_WCHISP"); env != "" {
		candidates = append(candidates, env)
	}
	if p, err := exec.LookPath("wchisp"); err == nil {
		candidates = append(candidates, p)
	}

	for _, c := range candidates {
		if c == "" {
			continue
		}
		if fi, err := os.Stat(c); err == nil && !fi.IsDir() {
			return c, nil
		}
		// Arduino recipes may hand us a name without the platform extension.
		withExe := c + ".exe"
		if fi, err := os.Stat(withExe); err == nil && !fi.IsDir() {
			return withExe, nil
		}
	}
	return "", fmt.Errorf("wchisp not found. It ships with this board package; if you " +
		"are running the tool standalone, set CHGAME_WCHISP to its full path.\n" +
		"It is needed only for the first flash of a blank board and for recovery; " +
		"normal uploads do not use it.")
}

const bootHelp = `no device is in factory ISP mode.

  1. Hold the BOOT button down.
  2. Switch the power OFF, then ON, while still holding BOOT.
  3. Release BOOT.
  4. Try again.

  BOOT must be held ACROSS the power cycle: the chip samples it only at reset,
  so pressing it while the board is already running does nothing.`

// wchispProbe confirms a device is in ISP mode and names it.
//
// Keyed on the POSITIVE signal - a "Device #" line - not on error wording:
// wchisp changed "Found 0 devices" to "Found 0 USB devices" between 0.2.3 and
// 0.3.0, and matching the failure text let a missing device sail past.
func wchispProbe(tool string) (string, error) {
	out, _ := exec.Command(tool, "probe").CombinedOutput()
	for _, line := range strings.Split(string(out), "\n") {
		if i := strings.Index(line, "Device #"); i >= 0 {
			return strings.TrimSpace(line[i+len("Device #"):]), nil
		}
	}
	return "", fmt.Errorf("%s", bootHelp)
}

func provision(bootPath, appPath, wchispPath string) error {
	tool, err := findWchisp(wchispPath)
	if err != nil {
		return err
	}

	boot, err := os.ReadFile(bootPath)
	if err != nil {
		return err
	}
	var app []byte
	if appPath != "" {
		if app, err = os.ReadFile(appPath); err != nil {
			return err
		}
	}

	fmt.Printf("wchisp      : %s\n", tool)
	dev, err := wchispProbe(tool)
	if err != nil {
		return err
	}
	fmt.Printf("device      : %s\n", dev)
	fmt.Printf("bootloader  : %s  (%d bytes)\n", filepath.Base(bootPath), len(boot))
	if app != nil {
		fmt.Printf("application : %s  (%d bytes)\n", filepath.Base(appPath), len(app))
	} else {
		fmt.Println("application : none - the device will come up in the bootloader,")
		fmt.Println("              enumerate as a COM port, and wait for a normal upload")
	}

	img, err := buildImage(boot, app)
	if err != nil {
		return err
	}

	// FULL CHIP ERASE FIRST.
	//
	// wchisp flash erases only the sectors it is about to write. Provisioning a
	// bootloader-only image therefore leaves the previous application AND its
	// metadata page intact, and the freshly written bootloader then does exactly
	// what it is designed to do: finds metadata that still checksums, and
	// launches the stale application. Burn Bootloader appeared to do nothing.
	//
	// This is why Arduino has a separate erase step before Burn Bootloader.
	// Doing it here rather than in that recipe means every provisioning path
	// gets it, including Upload Using Programmer.
	fmt.Println("erasing     : whole code flash")
	if out, err := exec.Command(tool, "erase").CombinedOutput(); err != nil {
		return fmt.Errorf("wchisp erase failed: %s", out)
	}

	tmp, err := os.CreateTemp("", "chgame_provision_*.bin")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if _, err := tmp.Write(img); err != nil {
		tmp.Close()
		return err
	}
	tmp.Close()

	out, err := exec.Command(tool, "flash", tmp.Name()).CombinedOutput()
	if err != nil || !strings.Contains(string(out), "Verify OK") {
		return fmt.Errorf("wchisp failed:\n%s", out)
	}

	fmt.Printf("written     : %d bytes, verified\n", len(img))
	fmt.Println("done        - the BOOT button is not needed again; use Upload from here on")
	return nil
}
