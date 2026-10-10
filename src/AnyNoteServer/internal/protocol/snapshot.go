// Package protocol implements the bounded, versioned wire format shared with the client.
package protocol

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"regexp"
	"strconv"
	"strings"
	"unicode/utf8"
)

type Limits struct {
	Nodes, Depth, NodeBytes, VaultBytes, JSONBytes int
}

var DefaultLimits = Limits{20000, 128, 16 << 20, 128 << 20, 192 << 20}
var uuid = regexp.MustCompile(`^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$`)
var uid = regexp.MustCompile(`^[0-9a-f]{16}$`)
var hash = regexp.MustCompile(`^[0-9a-f]{64}$`)
var decimal = regexp.MustCompile(`^(0|[1-9][0-9]*)$`)

// encoding/json replaces malformed UTF-8 and unpaired UTF-16 surrogates;
// the wire format rejects them instead, matching nlohmann/json.
func validJSONUnicode(raw []byte) bool {
	if !utf8.Valid(raw) {
		return false
	}
	for i := 0; i < len(raw); i++ {
		if raw[i] != '\\' {
			continue
		}
		i++
		if i >= len(raw) {
			return false
		}
		if raw[i] != 'u' {
			continue
		}
		if i+4 >= len(raw) {
			return false
		}
		n, err := strconv.ParseUint(string(raw[i+1:i+5]), 16, 16)
		if err != nil {
			return false
		}
		i += 4
		if n >= 0xdc00 && n <= 0xdfff {
			return false
		}
		if n < 0xd800 || n > 0xdbff {
			continue
		}
		if i+6 >= len(raw) || raw[i+1] != '\\' || raw[i+2] != 'u' {
			return false
		}
		low, err := strconv.ParseUint(string(raw[i+3:i+7]), 16, 16)
		if err != nil || low < 0xdc00 || low > 0xdfff {
			return false
		}
		i += 6
	}
	return true
}

type Node struct {
	SyncID       string  `json:"sync_id"`
	SourceID     string  `json:"source_id"`
	ParentSyncID *string `json:"parent_sync_id"`
	Sequence     int32   `json:"sequence"`
	Title        string  `json:"title"`
	NodeType     int     `json:"node_type"`
	CreatedTime  string  `json:"created_time"`
	ModifiedTime string  `json:"modified_time"`
	FormatType   int     `json:"format_type"`
	ContentHash  string  `json:"content_hash"`
	RTFBase64    string  `json:"rtf_base64"`
	RTF          []byte  `json:"-"`
}

type Snapshot struct {
	ProtocolVersion int    `json:"protocol_version"`
	SchemaVersion   int    `json:"schema_version"`
	UID             string `json:"uid"`
	Name            string `json:"name"`
	Nodes           []Node `json:"nodes"`
}

func integer(s string, positive bool) bool {
	if !decimal.MatchString(s) {
		return false
	}
	n, err := strconv.ParseInt(s, 10, 64)
	return err == nil && (!positive || n > 0)
}

func ValidateNode(n *Node, limits Limits) error {
	if !uuid.MatchString(n.SyncID) || (n.ParentSyncID != nil && !uuid.MatchString(*n.ParentSyncID)) {
		return errors.New("INVALID_UUID")
	}
	if !integer(n.SourceID, true) || !integer(n.CreatedTime, false) || !integer(n.ModifiedTime, false) || n.Sequence < 0 {
		return errors.New("INVALID_INTEGER")
	}
	if !utf8.ValidString(n.Title) || utf8.RuneCountInString(n.Title) > 1024 {
		return errors.New("LIMIT_EXCEEDED")
	}
	if n.NodeType != 0 || n.FormatType != 1 {
		return errors.New("UNSUPPORTED_FORMAT")
	}
	if !hash.MatchString(n.ContentHash) {
		return errors.New("INVALID_HASH")
	}
	if len(n.RTFBase64) > ((limits.NodeBytes+2)/3)*4 {
		return errors.New("LIMIT_EXCEEDED")
	}
	b, err := base64.StdEncoding.Strict().DecodeString(n.RTFBase64)
	if err != nil || base64.StdEncoding.EncodeToString(b) != n.RTFBase64 {
		return errors.New("INVALID_BASE64")
	}
	if len(b) > limits.NodeBytes {
		return errors.New("LIMIT_EXCEEDED")
	}
	digest := sha256.Sum256(b)
	if hex.EncodeToString(digest[:]) != n.ContentHash {
		return errors.New("HASH_MISMATCH")
	}
	n.RTF = b
	return nil
}

func ValidateTree(nodes []Node, limits Limits) error {
	if len(nodes) > limits.Nodes {
		return errors.New("LIMIT_EXCEEDED")
	}
	index := make(map[string]int, len(nodes))
	ids := make(map[string]bool, len(nodes))
	for i, n := range nodes {
		if _, ok := index[n.SyncID]; ok || !uuid.MatchString(n.SyncID) {
			return errors.New("DUPLICATE_SYNC_ID")
		}
		if ids[n.SourceID] || !integer(n.SourceID, true) {
			return errors.New("DUPLICATE_SOURCE_ID")
		}
		index[n.SyncID], ids[n.SourceID] = i, true
	}
	for i := range nodes {
		path := make(map[string]bool)
		current := &nodes[i]
		for {
			if path[current.SyncID] {
				return errors.New("TREE_CYCLE")
			}
			path[current.SyncID] = true
			if len(path) > limits.Depth {
				return errors.New("LIMIT_EXCEEDED")
			}
			if current.ParentSyncID == nil {
				break
			}
			parent, ok := index[*current.ParentSyncID]
			if !ok {
				return errors.New("MISSING_PARENT")
			}
			current = &nodes[parent]
		}
	}
	return nil
}

// object rejects repeated keys, including escaped spellings of the same key.
func object(d *json.Decoder, field func(string) error) error {
	t, err := d.Token()
	if err != nil || t != json.Delim('{') {
		return errors.New("INVALID_JSON")
	}
	seen := map[string]bool{}
	for d.More() {
		t, err = d.Token()
		key, ok := t.(string)
		if err != nil || !ok || seen[key] {
			return errors.New("DUPLICATE_FIELD")
		}
		seen[key] = true
		if err = field(key); err != nil {
			return err
		}
	}
	t, err = d.Token()
	if err != nil || t != json.Delim('}') {
		return errors.New("INVALID_JSON")
	}
	return nil
}

func readNode(d *json.Decoder, limits Limits) (Node, error) {
	var n Node
	fields := map[string]any{"sync_id": &n.SyncID, "source_id": &n.SourceID, "parent_sync_id": &n.ParentSyncID,
		"sequence": &n.Sequence, "title": &n.Title, "node_type": &n.NodeType, "created_time": &n.CreatedTime,
		"modified_time": &n.ModifiedTime, "format_type": &n.FormatType, "content_hash": &n.ContentHash, "rtf_base64": &n.RTFBase64}
	err := object(d, func(key string) error {
		p, ok := fields[key]
		if !ok {
			return errors.New("INVALID_NODE")
		}
		// Decode a single scalar, never a node/snapshot DOM; explicitly reject null
		// except parent_sync_id (encoding/json otherwise accepts null for scalars).
		var raw json.RawMessage
		if err := d.Decode(&raw); err != nil {
			return err
		}
		if string(raw) == "null" && key != "parent_sync_id" {
			return errors.New("INVALID_NODE")
		}
		if !validJSONUnicode(raw) {
			return errors.New("INVALID_UTF8")
		}
		if err := json.Unmarshal(raw, p); err != nil {
			return err
		}
		delete(fields, key)
		return nil
	})
	if err != nil {
		return n, err
	}
	if len(fields) != 0 {
		return n, errors.New("INVALID_NODE")
	}
	err = ValidateNode(&n, limits)
	return n, err
}

// ReadSnapshot retains only metadata when sink is supplied. The sink must target
// disposable staging storage: final validation may fail after some nodes arrive.
func ReadSnapshot(r io.Reader, limits Limits, sink func(Node) error) (Snapshot, error) {
	lr := &io.LimitedReader{R: r, N: int64(limits.JSONBytes) + 1}
	d := json.NewDecoder(lr)
	s, err := DecodeSnapshot(d, limits, sink)
	if err != nil {
		return s, err
	}
	if _, err = d.Token(); err != io.EOF {
		return s, errors.New("INVALID_JSON")
	}
	if lr.N <= 0 {
		return s, errors.New("LIMIT_EXCEEDED")
	}
	return s, nil
}

// DecodeSnapshot consumes one snapshot inside a bounded request envelope.
func DecodeSnapshot(d *json.Decoder, limits Limits, sink func(Node) error) (Snapshot, error) {
	var s Snapshot
	fields := map[string]bool{}
	total := 0
	err := object(d, func(key string) error {
		fields[key] = true
		switch key {
		case "protocol_version":
			return d.Decode(&s.ProtocolVersion)
		case "schema_version":
			return d.Decode(&s.SchemaVersion)
		case "uid":
			return d.Decode(&s.UID)
		case "name":
			var raw json.RawMessage
			if err := d.Decode(&raw); err != nil {
				return err
			}
			if !validJSONUnicode(raw) {
				return errors.New("INVALID_UTF8")
			}
			var name *string
			if err := json.Unmarshal(raw, &name); err != nil {
				return err
			}
			if name == nil {
				return errors.New("INVALID_SNAPSHOT")
			}
			s.Name = *name
			return nil
		case "nodes":
			t, err := d.Token()
			if err != nil || t != json.Delim('[') {
				return errors.New("INVALID_SNAPSHOT")
			}
			for d.More() {
				if len(s.Nodes) >= limits.Nodes {
					return errors.New("LIMIT_EXCEEDED")
				}
				n, err := readNode(d, limits)
				if err != nil {
					return err
				}
				if len(n.RTF) > limits.VaultBytes-total {
					return errors.New("LIMIT_EXCEEDED")
				}
				total += len(n.RTF)
				if sink != nil {
					if err = sink(n); err != nil {
						return err
					}
					n.RTF = nil
					n.RTFBase64 = ""
				}
				s.Nodes = append(s.Nodes, n)
			}
			t, err = d.Token()
			if err != nil || t != json.Delim(']') {
				return errors.New("INVALID_JSON")
			}
			return nil
		default:
			return errors.New("INVALID_SNAPSHOT")
		}
	})
	if err != nil {
		return s, err
	}
	if len(fields) != 5 || !fields["nodes"] || !uid.MatchString(s.UID) || !utf8.ValidString(s.Name) || utf8.RuneCountInString(s.Name) > 128 {
		return s, errors.New("INVALID_SNAPSHOT")
	}
	if s.ProtocolVersion != 1 || s.SchemaVersion != 1 {
		return s, errors.New("UNSUPPORTED_VERSION")
	}
	return s, ValidateTree(s.Nodes, limits)
}

// WriteSnapshot emits one node at a time. A failed writer means the request must
// be aborted; no receiver may commit until it has validated the complete body.
func WriteSnapshot(w io.Writer, s Snapshot, limits Limits) error {
	w = &limitedWriter{target: w, remaining: limits.JSONBytes}
	if s.ProtocolVersion != 1 || s.SchemaVersion != 1 || !uid.MatchString(s.UID) || !utf8.ValidString(s.Name) || utf8.RuneCountInString(s.Name) > 128 {
		return errors.New("INVALID_SNAPSHOT")
	}
	if err := ValidateTree(s.Nodes, limits); err != nil {
		return err
	}
	total := 0
	for _, node := range s.Nodes {
		if err := ValidateNode(&node, limits); err != nil {
			return err
		}
		total += len(node.RTF)
		if total > limits.VaultBytes {
			return errors.New("LIMIT_EXCEEDED")
		}
	}
	name, _ := json.Marshal(s.Name)
	if _, err := fmt.Fprintf(w, `{"protocol_version":1,"schema_version":1,"uid":%q,"name":%s,"nodes":[`, s.UID, name); err != nil {
		return err
	}
	enc := json.NewEncoder(w)
	enc.SetEscapeHTML(false)
	for i, node := range s.Nodes {
		if i > 0 {
			if _, err := io.WriteString(w, ","); err != nil {
				return err
			}
		}
		if err := enc.Encode(node); err != nil {
			return err
		}
	}
	_, err := io.WriteString(w, "]}")
	return err
}

type limitedWriter struct {
	target    io.Writer
	remaining int
}

func (w *limitedWriter) Write(p []byte) (int, error) {
	if len(p) > w.remaining {
		return 0, errors.New("LIMIT_EXCEEDED")
	}
	n, err := w.target.Write(p)
	w.remaining -= n
	if err == nil && n != len(p) {
		err = io.ErrShortWrite
	}
	return n, err
}

// NormalizeAccount matches the Windows client rule; passwords are never trimmed.
func NormalizeAccount(account string) (string, error) {
	if len(account) < 3 || len(account) > 32 {
		return "", errors.New("INVALID_ACCOUNT")
	}
	for _, c := range account {
		if !(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_') {
			return "", errors.New("INVALID_ACCOUNT")
		}
	}
	return strings.ToLower(account), nil
}

func ValidPassword(password string) bool {
	n := utf8.RuneCountInString(password)
	return utf8.ValidString(password) && n >= 8 && n <= 128 && len(password) <= 512
}
