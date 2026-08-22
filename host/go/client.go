package main

// Serial transport, port discovery, and the app -> bootloader transition.

import (
	"encoding/binary"
	"fmt"
	"time"

	"go.bug.st/serial"
)

// Development identifiers. See shared/chgame_usb_identity.h: this is the shared
// V-USB CDC pair and MUST be replaced with a real allocation before shipping.
const (
	chgameVID = "16C0"
	chgamePID = "27DD"
)

func eqFold(a, b string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		ca, cb := a[i], b[i]
		if 'a' <= ca && ca <= 'z' {
			ca -= 32
		}
		if 'a' <= cb && cb <= 'z' {
			cb -= 32
		}
		if ca != cb {
			return false
		}
	}
	return true
}

type Client struct {
	port    serial.Port
	name    string
	timeout time.Duration
	debug   bool
}

func openClient(name string, timeout time.Duration, debug bool) (*Client, error) {
	// Baud rate is irrelevant over USB CDC - the peripheral ignores it - except
	// for 1200, which is reserved as the upload-request signal.
	p, err := serial.Open(name, &serial.Mode{BaudRate: 115200})
	if err != nil {
		return nil, err
	}
	if err := p.SetReadTimeout(200 * time.Millisecond); err != nil {
		p.Close()
		return nil, err
	}
	c := &Client{port: p, name: name, timeout: timeout, debug: debug}
	p.ResetInputBuffer()
	return c, nil
}

func (c *Client) Close() {
	if c.port != nil {
		c.port.Close()
		c.port = nil
	}
}

func (c *Client) readExact(n int, deadline time.Time) ([]byte, error) {
	buf := make([]byte, 0, n)
	tmp := make([]byte, n)
	for len(buf) < n {
		if time.Now().After(deadline) {
			return nil, fmt.Errorf("timed out after %d of %d bytes", len(buf), n)
		}
		got, err := c.port.Read(tmp[:n-len(buf)])
		if err != nil {
			return nil, err
		}
		buf = append(buf, tmp[:got]...)
	}
	return buf, nil
}

func (c *Client) readFrame(deadline time.Time) (byte, []byte, error) {
	// Hunt for the two-byte start marker, discarding anything before it, so
	// stale bytes from a previous session cannot desynchronise us.
	window := make([]byte, 0, 2)
	one := make([]byte, 1)
	for {
		if time.Now().After(deadline) {
			return 0, nil, fmt.Errorf("timed out waiting for a frame")
		}
		n, err := c.port.Read(one)
		if err != nil {
			return 0, nil, err
		}
		if n == 0 {
			continue
		}
		window = append(window, one[0])
		if len(window) > 2 {
			window = window[1:]
		}
		if len(window) == 2 && window[0] == sof[0] && window[1] == sof[1] {
			break
		}
	}

	head, err := c.readExact(4, deadline)
	if err != nil {
		return 0, nil, err
	}
	cmd := head[1]
	length := int(binary.LittleEndian.Uint16(head[2:4]))

	var payload []byte
	if length > 0 {
		payload, err = c.readExact(length, deadline)
		if err != nil {
			return 0, nil, err
		}
	}
	tail, err := c.readExact(2, deadline)
	if err != nil {
		return 0, nil, err
	}

	body := append(append([]byte{}, head...), payload...)
	if got, want := binary.LittleEndian.Uint16(tail), crc16(body); got != want {
		return 0, nil, fmt.Errorf("frame CRC 0x%04X, computed 0x%04X", got, want)
	}
	if c.debug {
		fmt.Printf("  <- cmd 0x%02X len %d % X\n", cmd, length, payload)
	}
	return cmd &^ responseBit, payload, nil
}

func (c *Client) request(cmd byte, payload []byte, timeout time.Duration) ([]byte, error) {
	frame, err := buildFrame(cmd, payload)
	if err != nil {
		return nil, err
	}
	if c.debug {
		fmt.Printf("  -> cmd 0x%02X len %d % X\n", cmd, len(payload), frame)
	}
	if _, err := c.port.Write(frame); err != nil {
		return nil, err
	}
	if timeout == 0 {
		timeout = c.timeout
	}
	deadline := time.Now().Add(timeout)
	for {
		rcmd, rpayload, err := c.readFrame(deadline)
		if err != nil {
			return nil, err
		}
		if rcmd == cmd {
			return rpayload, nil
		}
		// Ignore a late reply to an earlier, timed-out request rather than
		// failing the current one.
		if c.debug {
			fmt.Printf("  (ignoring stale response to 0x%02X)\n", rcmd)
		}
	}
}

// check issues a command and fails unless the device answered OK.
func (c *Client) check(cmd byte, payload []byte, timeout time.Duration) ([]byte, error) {
	r, err := c.request(cmd, payload, timeout)
	if err != nil {
		return nil, err
	}
	if len(r) == 0 || r[0] != stOK {
		s := byte(0xFF)
		if len(r) > 0 {
			s = r[0]
		}
		return nil, &StatusError{Cmd: cmd, Status: s}
	}
	return r, nil
}

func (c *Client) hello() (*Hello, error) {
	r, err := c.request(cmdHello, nil, 0)
	if err != nil {
		return nil, err
	}
	return parseHello(r)
}

// uploadTouch asks a running sketch to reboot into the bootloader.
//
// Opening at 1200 baud sets the CDC line coding; dropping DTR is what fires it.
// The device acknowledges, detaches and resets, so the port disappearing here is
// success rather than failure.
func uploadTouch(name string) error {
	p, err := serial.Open(name, &serial.Mode{BaudRate: 1200})
	if err != nil {
		return err
	}
	_ = p.SetDTR(false)
	time.Sleep(50 * time.Millisecond)
	_ = p.Close()
	return nil
}

// waitForBootloader waits for a device answering HELLO in bootloader mode.
func waitForBootloader(timeout time.Duration, hint string, debug bool) (string, error) {
	deadline := time.Now().Add(timeout)
	var last error
	for time.Now().Before(deadline) {
		ports, _ := findPorts()
		if hint != "" {
			ports = preferFirst(ports, hint)
		}
		for _, name := range ports {
			h, err := quickHello(name, time.Second, debug)
			if err == nil && h.Mode == modeBootloader {
				return name, nil
			}
			last = err
		}
		time.Sleep(200 * time.Millisecond)
	}
	if last == nil {
		last = fmt.Errorf("no device answered")
	}
	return "", fmt.Errorf("no bootloader appeared within %s (%v)", timeout, last)
}

func preferFirst(list []string, want string) []string {
	out := make([]string, 0, len(list))
	for _, s := range list {
		if s == want {
			out = append(out, s)
		}
	}
	for _, s := range list {
		if s != want {
			out = append(out, s)
		}
	}
	return out
}

// quickHello probes a port with a HARD timeout.
//
// Necessary because a plain hello() can block forever: a sketch that prints but
// never calls Serial.read() lets the device's CDC receive buffer fill, the
// device NAKs, and the host write never completes. That describes most sketches,
// so probing has to assume it. go.bug.st/serial has no write timeout, so the
// port is closed from here to unblock the pending write.
//
// The probe goroutine may still be inside Read/Write when we close; that makes
// its call return an error, which it discards. Leaking it briefly is fine for a
// short-lived command-line tool and is much simpler than plumbing a cancellable
// transport through every call.
func quickHello(name string, timeout time.Duration, debug bool) (*Hello, error) {
	c, err := openClient(name, timeout, debug)
	if err != nil {
		return nil, err
	}
	type result struct {
		h   *Hello
		err error
	}
	ch := make(chan result, 1)
	go func() {
		h, err := c.hello()
		ch <- result{h, err}
	}()

	select {
	case r := <-ch:
		c.Close()
		return r.h, r.err
	case <-time.After(timeout):
		// Close asynchronously. On Windows, closing a handle with a pending
		// overlapped write can itself block, so waiting for it here would
		// reintroduce exactly the hang this function exists to prevent. The
		// handle is released when the write finally errors, or at process exit.
		go c.Close()
		return nil, fmt.Errorf("no response within %s", timeout)
	}
}

// ensureBootloader returns a port in bootloader mode.
//
// The touch is performed UNCONDITIONALLY rather than after probing to see
// whether we are already in the bootloader. Probing means writing a HELLO frame,
// and writing to an application that does not read Serial blocks forever - the
// device's receive buffer fills, it NAKs, and the write never completes. That
// describes most sketches, so it is not an edge case.
//
// The touch is safe to send to a bootloader: it is control transfers only
// (SET_LINE_CODING plus DTR), which cannot block on the bulk endpoint, and the
// bootloader's CDC has no upload-handshake handler, so it simply ignores it.
// The cost is a second or two when the device was already in the bootloader,
// which is a good trade for never hanging.
func ensureBootloader(name string, timeout time.Duration, debug bool) (string, error) {
	if err := uploadTouch(name); err != nil {
		return "", fmt.Errorf("1200-baud touch failed on %s: %w", name, err)
	}
	return waitForBootloader(timeout, name, debug)
}

// waitForApplication waits for the sketch to come back.
//
// An application is a CHGame port that does NOT answer HELLO. Sketches do not
// implement the bootloader protocol and must not: a sketch owns its Serial
// stream, so a responder in the core would eat bytes meant for the sketch and
// inject frames into its output.
func waitForApplication(timeout time.Duration, debug bool) (string, error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		ports, _ := findPorts()
		for _, name := range ports {
			h, err := quickHello(name, 800*time.Millisecond, debug)
			if err != nil || h.Mode != modeBootloader {
				return name, nil
			}
		}
		time.Sleep(250 * time.Millisecond)
	}
	return "", fmt.Errorf("application did not re-enumerate")
}
