package api

import (
	"anynote/server/internal/protocol"
	"anynote/server/internal/store"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

type harness struct {
	t      *testing.T
	db     *store.Store
	server *httptest.Server
	api    *Server
}

func setup(t *testing.T) *harness {
	t.Helper()
	db, err := store.Open(filepath.Join(t.TempDir(), "server.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	handler := New(db, true)
	handler.Log = slog.New(slog.NewTextHandler(io.Discard, nil))
	server := httptest.NewTLSServer(handler)
	t.Cleanup(func() { server.Close(); db.Close() })
	return &harness{t, db, server, handler}
}
func (h *harness) call(method, path, token string, body any) (int, map[string]json.RawMessage) {
	h.t.Helper()
	var raw []byte
	var err error
	if text, ok := body.(string); ok {
		raw = []byte(text)
	} else if body != nil {
		raw, err = json.Marshal(body)
		if err != nil {
			h.t.Fatal(err)
		}
	}
	req, err := http.NewRequest(method, h.server.URL+"/api/v1/"+path, bytes.NewReader(raw))
	if err != nil {
		h.t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/json")
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := h.server.Client().Do(req)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var result map[string]json.RawMessage
	if err = json.NewDecoder(response.Body).Decode(&result); err != nil {
		h.t.Fatal(err)
	}
	var id string
	if err = json.Unmarshal(result["request_id"], &id); err != nil || !protocol.UUID(id) {
		h.t.Fatalf("missing request ID: %v", result)
	}
	return response.StatusCode, result
}
func (h *harness) expect(method, path, token string, body any, status int) json.RawMessage {
	h.t.Helper()
	actual, result := h.call(method, path, token, body)
	if actual != status {
		h.t.Fatalf("%s %s: got %d want %d: %s", method, path, actual, status, result["error"])
	}
	return result["data"]
}
func (h *harness) account(name string) string {
	h.t.Helper()
	h.expect("POST", "auth/register", "", map[string]string{"username": name, "password": "test-password-123"}, 200)
	raw := h.expect("POST", "auth/login", "", map[string]string{"username": name, "password": "test-password-123"}, 200)
	var login store.LoginResult
	if err := json.Unmarshal(raw, &login); err != nil {
		h.t.Fatal(err)
	}
	if login.Token == "" {
		h.t.Fatal("empty token")
	}
	return login.Token
}
func snapshot() protocol.Snapshot {
	content := []byte{'{', '\\', 'r', 't', 'f', '1', 0, 255, '}'}
	hash := sha256.Sum256(content)
	n := protocol.Node{SyncID: store.UUID(), SourceID: "9223372036854775807", Title: "中文 root", CreatedTime: "0", ModifiedTime: "42", FormatType: 1,
		ContentHash: hex.EncodeToString(hash[:]), RTF: content, RTFBase64: base64.StdEncoding.EncodeToString(content)}
	return protocol.Snapshot{ProtocolVersion: 1, SchemaVersion: 1, UID: "0199abcdef012345", Name: "fixture", Nodes: []protocol.Node{n}}
}
func decodeOp(t *testing.T, raw json.RawMessage) store.Operation {
	t.Helper()
	var op store.Operation
	if err := json.Unmarshal(raw, &op); err != nil {
		t.Fatal(err)
	}
	return op
}
func version(op store.Operation) map[string]any {
	return map[string]any{"operation_id": store.UUID(), "expected_epoch": op.Epoch, "expected_revision": op.Revision}
}
func TestHTTPSVaultLifecycle(t *testing.T) {
	h := setup(t)
	alice := h.account("ALIce")
	bob := h.account("bob")
	h.expect("GET", "capabilities", alice, nil, 200)
	s := snapshot()
	path := "vaults/" + s.UID
	create := map[string]any{"precondition": map[string]any{"operation_id": store.UUID(), "create_only": true}, "snapshot": s}
	op := decodeOp(t, h.expect("PUT", path+"/snapshot", alice, create, 200))
	retry := decodeOp(t, h.expect("PUT", path+"/snapshot", alice, create, 200))
	if retry != op {
		t.Fatal("idempotent response differs")
	}
	changed := s
	changed.Name = "changed"
	create["snapshot"] = changed
	h.expect("PUT", path+"/snapshot", alice, create, 409)
	h.expect("GET", "operations/"+op.OperationID, bob, nil, 404)
	h.expect("GET", path+"/manifest", bob, nil, 404)
	h.expect("GET", path+"/snapshot?epoch="+op.Epoch+"&revision="+op.Revision, bob, nil, 404)
	h.expect("DELETE", path, bob, map[string]any{"precondition": version(op)}, 404)
	h.expect("PUT", path+"/snapshot", bob, map[string]any{"precondition": version(op), "snapshot": s}, 404)
	var list store.VaultList
	if err := json.Unmarshal(h.expect("GET", "vaults", bob, nil, 200), &list); err != nil || len(list.Vaults) != 0 {
		t.Fatal("account leak")
	}
	raw := h.expect("GET", path+"/manifest", alice, nil, 200)
	if bytes.Contains(raw, []byte("rtf_base64")) {
		t.Fatal("manifest contains RTF")
	}
	data := h.expect("GET", path+"/snapshot?epoch="+op.Epoch+"&revision=1", alice, nil, 200)
	var read struct {
		Snapshot protocol.Snapshot `json:"snapshot"`
	}
	if err := json.Unmarshal(data, &read); err != nil {
		t.Fatal(err)
	}
	if len(read.Snapshot.Nodes) != 1 || read.Snapshot.Nodes[0].RTFBase64 != s.Nodes[0].RTFBase64 {
		t.Fatal("binary content changed")
	}
	h.expect("GET", path+"/snapshot", alice, nil, 400)
	h.expect("GET", path+"/snapshot?epoch="+op.Epoch+"&revision=2", alice, nil, 409)
	// Exactly one same-version contender commits; the second sees the new version.
	statuses := make(chan int, 2)
	var group sync.WaitGroup
	for _, remark := range []string{"first", "second"} {
		group.Add(1)
		go func(remark string) {
			defer group.Done()
			code, _ := h.call("PATCH", path+"/remark", alice, map[string]any{"precondition": version(op), "remark": remark})
			statuses <- code
		}(remark)
	}
	group.Wait()
	close(statuses)
	counts := map[int]int{}
	for status := range statuses {
		counts[status]++
	}
	if counts[200] != 1 || counts[409] != 1 {
		t.Fatalf("concurrent statuses: %v", counts)
	}
	op.Revision = "2"
	// Rehearsed tree: missing ancestor is supplemented, with no unrelated siblings.
	root := s.Nodes[0].SyncID
	ancestor := s.Nodes[0]
	ancestor.SyncID = store.UUID()
	ancestor.SourceID = "2"
	ancestor.Title = "ancestor"
	ancestor.ParentSyncID = nil
	source := s
	source.Nodes = append([]protocol.Node{}, s.Nodes...)
	source.Nodes[0].ParentSyncID = &ancestor.SyncID
	source.Nodes = append(source.Nodes, ancestor)
	op = decodeOp(t, h.expect("PUT", path+"/nodes/"+root+"/subtree", alice, map[string]any{"precondition": version(op), "source": source}, 200))
	subtree := h.expect("GET", path+"/nodes/"+root+"/subtree?epoch="+op.Epoch+"&revision="+op.Revision, alice, nil, 200)
	if !bytes.Contains(subtree, []byte(ancestor.SyncID)) {
		t.Fatal("ancestor missing")
	}
	// SQL failure after delete must roll back the entire vault and operation receipt.
	if _, err := h.db.DB.Exec("CREATE TRIGGER reject_write BEFORE INSERT ON nodes BEGIN SELECT RAISE(ABORT,'test fault'); END"); err != nil {
		t.Fatal(err)
	}
	failed := version(op)
	h.expect("PUT", path+"/snapshot", alice, map[string]any{"precondition": failed, "snapshot": s}, 503)
	h.expect("GET", "operations/"+failed["operation_id"].(string), alice, nil, 404)
	h.expect("GET", path+"/snapshot?epoch="+op.Epoch+"&revision="+op.Revision, alice, nil, 200)
	if _, err := h.db.DB.Exec("DROP TRIGGER reject_write"); err != nil {
		t.Fatal(err)
	}
	op = decodeOp(t, h.expect("DELETE", path+"/nodes/"+root+"/subtree", alice, map[string]any{"precondition": version(op)}, 200))
	if op.Deleted != 1 {
		t.Fatal("wrong subtree deletion count")
	}
	deletion := map[string]any{"precondition": version(op)}
	deleted := decodeOp(t, h.expect("DELETE", path, alice, deletion, 200))
	h.expect("DELETE", path, alice, deletion, 200)
	h.expect("GET", path+"/manifest", alice, nil, 404)
	create["snapshot"] = s
	create["precondition"] = map[string]any{"operation_id": store.UUID(), "create_only": true}
	recreated := decodeOp(t, h.expect("PUT", path+"/snapshot", alice, create, 200))
	if recreated.Epoch == deleted.Epoch {
		t.Fatal("epoch reused after recreation")
	}
	h.expect("PUT", path+"/snapshot", alice, map[string]any{"precondition": version(op), "snapshot": s}, 409)
	h.expect("POST", "auth/password", alice, map[string]string{"old_password": "test-password-123", "new_password": "changed-password-123"}, 200)
	h.expect("GET", "vaults", alice, nil, 401)
	h.expect("GET", "vaults", bob, nil, 200)
	h.expect("POST", "auth/logout", bob, map[string]any{}, 200)
	h.expect("GET", "vaults", bob, nil, 401)
}
func TestMalformedQuotaAndSessionExpiry(t *testing.T) {
	h := setup(t)
	token := h.account("test_user")
	s := snapshot()
	path := "vaults/" + s.UID + "/snapshot"
	h.expect("POST", "auth/login", "", `{"username":"test_user","username":"other","password":"test-password-123"}`, 400)
	request := map[string]any{"precondition": map[string]any{"operation_id": store.UUID(), "create_only": true}, "snapshot": s}
	broken := s
	broken.Nodes = append([]protocol.Node{}, s.Nodes...)
	broken.Nodes[0].ContentHash = strings.Repeat("0", 64)
	request["snapshot"] = broken
	h.expect("PUT", path, token, request, 422)
	request["snapshot"] = s
	h.db.AccountBytes = 1
	h.expect("PUT", path, token, request, 413)
	h.db.AccountBytes = 2 << 30
	h.expect("PUT", path, token, request, 200)
	h.expect("PUT", path, token, map[string]any{"snapshot": s}, 400)
	if _, err := h.db.DB.Exec("UPDATE sessions SET expires_at=0"); err != nil {
		t.Fatal(err)
	}
	h.expect("GET", "vaults", token, nil, 401)
	h.api.RegistrationEnabled = false
	h.expect("POST", "auth/register", "", map[string]string{"username": "new_user", "password": "test-password-123"}, 403)
	req := httptest.NewRequest("GET", "http://localhost/api/v1/vaults", nil)
	recorder := httptest.NewRecorder()
	h.api.ServeHTTP(recorder, req)
	if recorder.Code != 400 {
		t.Fatal("plaintext accepted")
	}
}
func TestAuthenticationRateLimit(t *testing.T) {
	h := setup(t)
	for i := 0; i < 10; i++ {
		h.expect("POST", "auth/login", "", `{}`, 400)
	}
	h.expect("POST", "auth/login", "", `{}`, 429)
	if err := h.db.Register(context.Background(), "normalized", "password-123"); err != nil {
		t.Fatal(err)
	}
	if err := h.db.Register(context.Background(), "NORMALIZED", "password-456"); err != store.ErrConflict {
		t.Fatal("normalized duplicate accepted", err)
	}
}
