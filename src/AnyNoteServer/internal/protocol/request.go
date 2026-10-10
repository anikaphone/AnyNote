package protocol

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"unicode/utf8"
)

func UUID(s string) bool       { return uuid.MatchString(s) }
func UID(s string) bool        { return uid.MatchString(s) }
func Positive64(s string) bool { return integer(s, true) }

// StrictObject is for small control messages; snapshots use the streaming decoder.
func StrictObject(raw []byte, allowed ...string) (map[string]json.RawMessage, error) {
	if len(raw) > 64<<10 || !validJSONUnicode(raw) {
		return nil, errors.New("INVALID_JSON")
	}
	result := map[string]json.RawMessage{}
	d := json.NewDecoder(bytes.NewReader(raw))
	err := object(d, func(key string) error {
		found := false
		for _, a := range allowed {
			if a == key {
				found = true
				break
			}
		}
		if !found {
			return errors.New("INVALID_REQUEST")
		}
		var value json.RawMessage
		if err := d.Decode(&value); err != nil {
			return err
		}
		if bytes.Equal(value, []byte("null")) {
			return errors.New("INVALID_REQUEST")
		}
		result[key] = value
		return nil
	})
	if err != nil {
		return nil, err
	}
	if _, err = d.Token(); err != io.EOF {
		return nil, errors.New("INVALID_JSON")
	}
	return result, nil
}

type Precondition struct {
	OperationID string `json:"operation_id"`
	CreateOnly  bool   `json:"create_only,omitempty"`
	Epoch       string `json:"expected_epoch,omitempty"`
	Revision    string `json:"expected_revision,omitempty"`
}

func ReadPrecondition(raw []byte) (Precondition, error) {
	var p Precondition
	fields, err := StrictObject(raw, "operation_id", "create_only", "expected_epoch", "expected_revision")
	if err != nil {
		return p, err
	}
	if err = json.Unmarshal(raw, &p); err != nil {
		return p, err
	}
	if !UUID(p.OperationID) {
		return p, errors.New("INVALID_PRECONDITION")
	}
	if p.CreateOnly {
		if len(fields) != 2 {
			return p, errors.New("INVALID_PRECONDITION")
		}
	} else {
		if len(fields) != 3 || !UUID(p.Epoch) || !Positive64(p.Revision) {
			return p, errors.New("INVALID_PRECONDITION")
		}
	}
	return p, nil
}

type WriteRequest struct {
	Precondition Precondition
	Snapshot     Snapshot
	Remark       string
}

func ReadWriteRequest(r io.Reader, limits Limits, kind string) (WriteRequest, error) {
	var result WriteRequest
	lr := &io.LimitedReader{R: r, N: int64(limits.JSONBytes) + 1}
	d := json.NewDecoder(lr)
	fields := map[string]bool{}
	err := object(d, func(key string) error {
		fields[key] = true
		switch key {
		case "precondition":
			var raw json.RawMessage
			if err := d.Decode(&raw); err != nil {
				return err
			}
			p, err := ReadPrecondition(raw)
			result.Precondition = p
			return err
		case "snapshot", "source":
			if (kind != "snapshot" || key != "snapshot") && (kind != "subtree" || key != "source") {
				return errors.New("INVALID_REQUEST")
			}
			s, err := DecodeSnapshot(d, limits, nil)
			result.Snapshot = s
			return err
		case "remark":
			if kind != "remark" {
				return errors.New("INVALID_REQUEST")
			}
			var raw json.RawMessage
			if err := d.Decode(&raw); err != nil {
				return err
			}
			if !validJSONUnicode(raw) || bytes.Equal(raw, []byte("null")) {
				return errors.New("INVALID_REQUEST")
			}
			if err := json.Unmarshal(raw, &result.Remark); err != nil {
				return err
			}
			if utf8.RuneCountInString(result.Remark) > 2000 {
				return errors.New("LIMIT_EXCEEDED")
			}
			return nil
		default:
			return errors.New("INVALID_REQUEST")
		}
	})
	if err != nil {
		return result, err
	}
	if lr.N <= 0 {
		return result, errors.New("LIMIT_EXCEEDED")
	}
	if _, err = d.Token(); err != io.EOF {
		return result, errors.New("INVALID_JSON")
	}
	expected := 2
	if kind == "delete" || kind == "delete_subtree" {
		expected = 1
	}
	if !fields["precondition"] || len(fields) != expected || (kind != "snapshot" && result.Precondition.CreateOnly) {
		return result, errors.New("INVALID_PRECONDITION")
	}
	return result, nil
}
