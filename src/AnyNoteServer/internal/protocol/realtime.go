package protocol

import (
	"encoding/json"
	"errors"
	"io"
)

type Batch struct {
	Precondition Precondition `json:"precondition"`
	Upserts      []Node       `json:"upserts"`
	Deletes      []string     `json:"deletes"`
}

// A batch is not a tree: parents can already exist at the destination. Validate
// the resulting tree in the transaction, after assigning destination IDs.
func ReadBatch(r io.Reader, limits Limits) (Batch, error) {
	b := Batch{Upserts: []Node{}, Deletes: []string{}}
	lr := &io.LimitedReader{R: r, N: int64(limits.JSONBytes) + 1}
	d := json.NewDecoder(lr)
	fields := 0
	ids := map[string]bool{}
	bytes := 0
	err := object(d, func(key string) error {
		fields++
		if key == "precondition" {
			var raw json.RawMessage
			if err := d.Decode(&raw); err != nil {
				return err
			}
			p, err := ReadPrecondition(raw)
			b.Precondition = p
			if err != nil {
				return err
			}
			if p.CreateOnly {
				return errors.New("INVALID_PRECONDITION")
			}
			return nil
		}
		if key != "upserts" && key != "deletes" {
			return errors.New("INVALID_REQUEST")
		}
		token, err := d.Token()
		if err != nil || token != json.Delim('[') {
			return errors.New("INVALID_REQUEST")
		}
		for d.More() {
			if (key == "upserts" && len(b.Upserts) >= limits.Nodes) || (key == "deletes" && len(b.Deletes) >= limits.Nodes) {
				return errors.New("LIMIT_EXCEEDED")
			}
			var id string
			if key == "upserts" {
				n, err := readNode(d, limits)
				if err != nil {
					return err
				}
				id = n.SyncID
				bytes += len(n.RTF)
				if bytes > limits.VaultBytes {
					return errors.New("LIMIT_EXCEEDED")
				}
				n.RTFBase64 = ""
				b.Upserts = append(b.Upserts, n)
			} else {
				if err := d.Decode(&id); err != nil || !UUID(id) {
					return errors.New("INVALID_UUID")
				}
				b.Deletes = append(b.Deletes, id)
			}
			if ids[id] {
				return errors.New("DUPLICATE_SYNC_ID")
			}
			ids[id] = true
		}
		_, err = d.Token()
		return err
	})
	if err != nil {
		return b, err
	}
	if fields != 3 || !UUID(b.Precondition.OperationID) {
		return b, errors.New("INVALID_REQUEST")
	}
	if lr.N <= 0 {
		return b, errors.New("LIMIT_EXCEEDED")
	}
	if _, err = d.Token(); err != io.EOF {
		return b, errors.New("INVALID_JSON")
	}
	return b, nil
}
