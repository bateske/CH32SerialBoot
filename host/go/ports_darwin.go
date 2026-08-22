//go:build darwin

package main

// Port discovery on macOS.
//
// go.bug.st/serial's enumerator needs cgo on darwin (it queries IOKit), which
// would make the binary impossible to cross-compile from a single build machine
// and would drag a C toolchain into the release process. Since Arduino ALWAYS
// passes -port explicitly from platform.txt, VID/PID discovery is only a
// convenience for running the tool by hand.
//
// So on macOS we fall back to matching device names and let the protocol probe
// settle what is actually there. The cost is that an unrelated USB-serial
// adapter may be listed; it will simply fail to answer HELLO, and if more than
// one candidate is present the tool asks for -port rather than guessing.

import (
	"strings"

	"go.bug.st/serial"
)

func findPorts() ([]string, error) {
	all, err := serial.GetPortsList()
	if err != nil {
		return nil, err
	}
	var out []string
	for _, name := range all {
		// macOS exposes both /dev/tty.* (blocks until DCD) and /dev/cu.* for the
		// same device. Only cu.* is usable for a device that does not assert
		// carrier detect, which is every USB CDC device.
		if strings.HasPrefix(name, "/dev/cu.usbmodem") {
			out = append(out, name)
		}
	}
	return out, nil
}
