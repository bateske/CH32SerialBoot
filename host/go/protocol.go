package main

// CHGame bootloader wire protocol.
//
// Mirrors bootloader/src/proto.{h,c} and host/py/chgame/protocol.py.
// docs/protocol.md is normative; test/protocol/vectors.json keeps the three
// implementations honest with each other.
//
// Frame:
//
//	'C' 'G' | ver u8 | cmd u8 | len u16 LE | payload[len] | crc16 u16 LE
//	         \______________ CRC-16/CCITT-FALSE ________/

import (
	"encoding/binary"
	"fmt"
)

const (
	protoVersion = 1
	maxPayload   = 512
	responseBit  = 0x80
)

var sof = []byte{'C', 'G'}

const (
	cmdHello        = 0x01
	cmdBegin        = 0x02
	cmdWrite        = 0x03
	cmdEnd          = 0x04
	cmdRun          = 0x05
	cmdStatus       = 0x06
	cmdAbort        = 0x07
	cmdRead         = 0x08
	cmdDevUnlock    = 0x40
	cmdDevWriteBoot = 0x41
)

const (
	stOK = 0x00
)

var statusNames = map[byte]string{
	0x00: "OK",
	0x01: "ERR_BADCMD",
	0x02: "ERR_STATE",
	0x03: "ERR_RANGE",
	0x04: "ERR_SIZE",
	0x05: "ERR_CRC",
	0x06: "ERR_FLASH",
	0x07: "ERR_LOCKED",
	0x08: "ERR_FRAME",
	0x09: "ERR_NOTIMPL",
}

const (
	modeBootloader  = 1
	modeApplication = 2
)

var appStateNames = map[byte]string{
	0: "valid", 1: "no metadata", 2: "bad length", 3: "bad CRC",
}

func statusName(s byte) string {
	if n, ok := statusNames[s]; ok {
		return n
	}
	return fmt.Sprintf("0x%02X", s)
}

// StatusError carries the device's own refusal code, so callers can tell
// "image too large" from "flash failed" from "CRC mismatch".
type StatusError struct {
	Cmd    byte
	Status byte
}

func (e *StatusError) Error() string {
	return fmt.Sprintf("command 0x%02X returned %s", e.Cmd, statusName(e.Status))
}

// crc16 is CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection.
func crc16(data []byte) uint16 {
	crc := uint16(0xFFFF)
	for _, b := range data {
		crc ^= uint16(b) << 8
		for i := 0; i < 8; i++ {
			if crc&0x8000 != 0 {
				crc = (crc << 1) ^ 0x1021
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

func buildFrame(cmd byte, payload []byte) ([]byte, error) {
	if len(payload) > maxPayload {
		return nil, fmt.Errorf("payload %d exceeds %d", len(payload), maxPayload)
	}
	body := make([]byte, 4+len(payload))
	body[0] = protoVersion
	body[1] = cmd
	binary.LittleEndian.PutUint16(body[2:4], uint16(len(payload)))
	copy(body[4:], payload)

	out := make([]byte, 0, 2+len(body)+2)
	out = append(out, sof...)
	out = append(out, body...)
	out = binary.LittleEndian.AppendUint16(out, crc16(body))
	return out, nil
}

// Hello is the device identification returned by cmdHello.
type Hello struct {
	ProtoVersion byte
	Mode         byte
	AppState     byte
	BootVersion  uint16
	AppStart     uint32
	AppMaxSize   uint32
	PageSize     uint16
	MaxPayload   uint16
	UID          []byte
}

func parseHello(p []byte) (*Hello, error) {
	if len(p) < 30 {
		return nil, fmt.Errorf("HELLO payload is %d bytes, expected at least 30", len(p))
	}
	if p[0] != stOK {
		return nil, &StatusError{Cmd: cmdHello, Status: p[0]}
	}
	return &Hello{
		ProtoVersion: p[1],
		Mode:         p[2],
		AppState:     p[3],
		BootVersion:  binary.LittleEndian.Uint16(p[4:6]),
		AppStart:     binary.LittleEndian.Uint32(p[6:10]),
		AppMaxSize:   binary.LittleEndian.Uint32(p[10:14]),
		PageSize:     binary.LittleEndian.Uint16(p[14:16]),
		MaxPayload:   binary.LittleEndian.Uint16(p[16:18]),
		UID:          append([]byte(nil), p[18:30]...),
	}, nil
}

func (h *Hello) modeName() string {
	switch h.Mode {
	case modeBootloader:
		return "bootloader"
	case modeApplication:
		return "application"
	}
	return fmt.Sprintf("%d", h.Mode)
}

func (h *Hello) describe() string {
	return fmt.Sprintf(
		"mode          : %s\n"+
			"protocol      : v%d\n"+
			"bootloader    : v%d\n"+
			"application   : %s\n"+
			"app region    : 0x%04X .. 0x%04X  (%d bytes)\n"+
			"flash page    : %d bytes\n"+
			"max payload   : %d bytes\n"+
			"chip UID      : %X",
		h.modeName(), h.ProtoVersion, h.BootVersion,
		appStateNames[h.AppState],
		h.AppStart, h.AppStart+h.AppMaxSize, h.AppMaxSize,
		h.PageSize, h.MaxPayload, h.UID)
}
