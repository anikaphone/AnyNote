package protocol

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

func TestSharedFixtures(t *testing.T) {
	files, err := filepath.Glob("../../../../tests/fixtures/sync-v1/*.json")
	if err != nil || len(files) < 8 {
		t.Fatalf("fixtures: %d %v", len(files), err)
	}
	for _, file := range files {
		t.Run(filepath.Base(file), func(t *testing.T) {
			b, err := os.ReadFile(file)
			if err != nil {
				t.Fatal(err)
			}
			_, err = ReadSnapshot(bytes.NewReader(b), DefaultLimits, nil)
			if (err == nil) != strings.HasPrefix(filepath.Base(file), "valid-") {
				t.Fatalf("unexpected result: %v", err)
			}
		})
	}
}

func sample(id int) Node {
	digest := sha256.Sum256(nil)
	return Node{SyncID: fmt.Sprintf("00000000-0000-4000-8000-%012x", id), SourceID: fmt.Sprint(id),
		CreatedTime: "0", ModifiedTime: "0", FormatType: 1, ContentHash: hex.EncodeToString(digest[:])}
}

func TestRoundTripAndReject(t *testing.T) {
	n := sample(1)
	rtf := []byte{'{', '\\', 'r', 't', 'f', '1', 0, 255, '}'}
	digest := sha256.Sum256(rtf)
	n.RTFBase64, n.ContentHash = base64.StdEncoding.EncodeToString(rtf), hex.EncodeToString(digest[:])
	s := Snapshot{1, 1, "0123456789abcdef", "中文 😀", []Node{n}}
	var b bytes.Buffer
	if err := WriteSnapshot(&b, s, DefaultLimits); err != nil {
		t.Fatal(err)
	}
	out, err := ReadSnapshot(bytes.NewReader(b.Bytes()), DefaultLimits, nil)
	if err != nil || !bytes.Equal(out.Nodes[0].RTF, rtf) {
		t.Fatalf("round trip: %v", err)
	}
	for _, bad := range []string{"01", "-1", "9223372036854775808", "+2", "1.0"} {
		badNode := n
		badNode.SourceID = bad
		s.Nodes[0] = badNode
		data, _ := json.Marshal(s)
		if _, err = ReadSnapshot(bytes.NewReader(data), DefaultLimits, nil); err == nil {
			t.Fatalf("accepted integer %s", bad)
		}
	}
	for _, field := range []string{"source_id", "sequence", "title", "node_type", "created_time", "format_type", "content_hash", "rtf_base64"} {
		var object map[string]any
		if err := json.Unmarshal(b.Bytes(), &object); err != nil {
			t.Fatal(err)
		}
		object["nodes"].([]any)[0].(map[string]any)[field] = nil
		data, _ := json.Marshal(object)
		if _, err = ReadSnapshot(bytes.NewReader(data), DefaultLimits, nil); err == nil {
			t.Fatalf("accepted null %s", field)
		}
	}
	limits := DefaultLimits
	limits.JSONBytes = b.Len() - 1
	if _, err = ReadSnapshot(bytes.NewReader(b.Bytes()), limits, nil); err == nil {
		t.Fatal("accepted overlimit JSON")
	}
	s.Nodes[0] = n
	if WriteSnapshot(io.Discard, s, limits) == nil {
		t.Fatal("writer accepted overlimit JSON")
	}
	limits = DefaultLimits
	limits.NodeBytes = len(rtf) - 1
	if _, err = ReadSnapshot(bytes.NewReader(b.Bytes()), limits, nil); err == nil {
		t.Fatal("accepted overlimit RTF")
	}
	limits = DefaultLimits
	limits.VaultBytes = len(rtf) - 1
	if _, err = ReadSnapshot(bytes.NewReader(b.Bytes()), limits, nil); err == nil {
		t.Fatal("accepted overlimit vault")
	}
	n.RTFBase64 = "AA==\n"
	if ValidateNode(&n, DefaultLimits) == nil {
		t.Fatal("accepted noncanonical base64")
	}
}

func FuzzReadSnapshot(f *testing.F) {
	files, _ := filepath.Glob("../../../../tests/fixtures/sync-v1/*.json")
	for _, path := range files {
		b, err := os.ReadFile(path)
		if err != nil {
			f.Fatal(err)
		}
		f.Add(b)
	}
	f.Fuzz(func(t *testing.T, b []byte) {
		limits := Limits{64, 16, 4096, 8192, 16384}
		_, _ = ReadSnapshot(bytes.NewReader(b), limits, func(Node) error { return nil })
	})
}

func TestTree(t *testing.T) {
	nodes := []Node{sample(1), sample(2)}
	nodes[1].ParentSyncID = &nodes[0].SyncID
	if err := ValidateTree(nodes, DefaultLimits); err != nil {
		t.Fatal(err)
	}
	nodes[0].ParentSyncID = &nodes[1].SyncID
	if ValidateTree(nodes, DefaultLimits) == nil {
		t.Fatal("accepted cycle")
	}
	nodes[0].ParentSyncID = nil
	missing := sample(3).SyncID
	nodes[1].ParentSyncID = &missing
	if ValidateTree(nodes, DefaultLimits) == nil {
		t.Fatal("accepted missing parent")
	}
	nodes[1].ParentSyncID = nil
	nodes[1].SourceID = nodes[0].SourceID
	if ValidateTree(nodes, DefaultLimits) == nil {
		t.Fatal("accepted duplicate source id")
	}
}

func TestAccountRules(t *testing.T) {
	if a, err := NormalizeAccount("Ab_C1"); err != nil || a != "ab_c1" {
		t.Fatal(a, err)
	}
	for _, bad := range []string{"ab", "中文账号", "a b", "a/b", strings.Repeat("a", 33)} {
		if _, err := NormalizeAccount(bad); err == nil {
			t.Fatal("accepted account", bad)
		}
	}
	if !ValidPassword("        ") || !ValidPassword(strings.Repeat("😀", 128)) || ValidPassword(strings.Repeat("a", 129)) {
		t.Fatal("password boundary")
	}
}

func TestScale(t *testing.T) {
	if os.Getenv("ANYNOTE_SCALE_TEST") != "1" {
		t.Skip("set ANYNOTE_SCALE_TEST=1 for 128 MiB / 20000 node probe")
	}
	reader, writer := io.Pipe()
	defer reader.Close()
	go func() {
		_, err := io.WriteString(writer, `{"protocol_version":1,"schema_version":1,"uid":"0123456789abcdef","name":"scale","nodes":[`)
		for i := 1; err == nil && i <= 20000; i++ {
			n := sample(i)
			if i <= 8 {
				data := bytes.Repeat([]byte{'x'}, 16<<20)
				digest := sha256.Sum256(data)
				n.ContentHash, n.RTFBase64 = hex.EncodeToString(digest[:]), base64.StdEncoding.EncodeToString(data)
			}
			if i > 1 {
				_, err = io.WriteString(writer, ",")
			}
			if err == nil {
				err = json.NewEncoder(writer).Encode(n)
			}
		}
		if err == nil {
			_, err = io.WriteString(writer, "]}")
		}
		writer.CloseWithError(err)
	}()
	start, total := time.Now(), 0
	s, err := ReadSnapshot(reader, DefaultLimits, func(n Node) error { total += len(n.RTF); return nil })
	if err != nil || len(s.Nodes) != 20000 || total != 128<<20 {
		t.Fatalf("scale: %d %d %v", len(s.Nodes), total, err)
	}
	var mem runtime.MemStats
	runtime.ReadMemStats(&mem)
	t.Logf("nodes=%d raw_bytes=%d elapsed=%s heap_sys=%d total_alloc=%d", len(s.Nodes), total, time.Since(start), mem.HeapSys, mem.TotalAlloc)
}
