//go:build !darwin

package main

// Port discovery on Windows and Linux, where go.bug.st/serial's enumerator is
// pure Go and can report USB VID/PID.

import "go.bug.st/serial/enumerator"

// findPorts returns the serial ports that look like a CHGame device.
//
// The application and the bootloader deliberately present IDENTICAL USB
// descriptors, so this cannot distinguish them - that is exactly what keeps a
// single stable COM port across an upload. Mode is established by protocol
// probe, not by the descriptors.
func findPorts() ([]string, error) {
	list, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return nil, err
	}
	var out []string
	for _, p := range list {
		if p.IsUSB && eqFold(p.VID, chgameVID) && eqFold(p.PID, chgamePID) {
			out = append(out, p.Name)
		}
	}
	return out, nil
}
