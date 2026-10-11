package api

import (
	"anynote/server/internal/protocol"
	"anynote/server/internal/store"
	"encoding/json"
	"testing"
)

func TestRealtimeBatchAtomicityIdempotencyAndDeletion(t *testing.T) {
	h := setup(t)
	token := h.account("realtime")
	other := h.account("other")
	s := snapshot()
	s.Nodes[0].SourceID = "1"
	path := "vaults/" + s.UID
	op := decodeOp(t, h.expect("PUT", path+"/snapshot", token, map[string]any{"precondition": map[string]any{"operation_id": store.UUID(), "create_only": true}, "snapshot": s}, 200))
	initial := op
	node := s.Nodes[0]
	node.Title = "changed"
	batch := map[string]any{"precondition": version(op), "upserts": []protocol.Node{node}, "deletes": []string{}}
	op = decodeOp(t, h.expect("POST", path+"/sync-batch", token, batch, 200))
	retry := decodeOp(t, h.expect("POST", path+"/sync-batch", token, batch, 200))
	if retry != op {
		t.Fatal("retry was not idempotent")
	}
	h.expect("POST", path+"/sync-batch", other, batch, 404)
	h.expect("GET", path+"/sync-manifest", other, nil, 404)
	var manifest struct {
		Nodes []protocol.Node `json:"nodes"`
	}
	if err := json.Unmarshal(h.expect("GET", path+"/sync-manifest", token, nil, 200), &manifest); err != nil || len(manifest.Nodes) != 1 || manifest.Nodes[0].Title != "changed" || manifest.Nodes[0].RTFBase64 != "" {
		t.Fatal("bad metadata", err)
	}
	node.Title = "different retry"
	batch["upserts"] = []protocol.Node{node}
	h.expect("POST", path+"/sync-batch", token, batch, 409)
	batch["precondition"] = version(initial)
	h.expect("POST", path+"/sync-batch", token, batch, 409)
	// An invalid final tree must roll back revision, content, history and receipt.
	missing := store.UUID()
	node.ParentSyncID = &missing
	batch["upserts"] = []protocol.Node{node}
	batch["precondition"] = version(op)
	h.expect("POST", path+"/sync-batch", token, batch, 400)
	h.expect("GET", path+"/nodes/"+node.SyncID+"/content?epoch="+op.Epoch+"&revision="+op.Revision, token, nil, 200)
	// Add a child without transmitting the unchanged parent's body.
	child := s.Nodes[0]
	child.SyncID = store.UUID()
	child.SourceID = "2"
	child.ParentSyncID = &s.Nodes[0].SyncID
	batch["precondition"] = version(op)
	batch["upserts"] = []protocol.Node{child}
	op = decodeOp(t, h.expect("POST", path+"/sync-batch", token, batch, 200))
	batch["precondition"] = version(op)
	batch["upserts"] = []protocol.Node{}
	batch["deletes"] = []string{s.Nodes[0].SyncID}
	h.expect("POST", path+"/sync-batch", token, batch, 400)
	batch["deletes"] = []string{s.Nodes[0].SyncID, child.SyncID}
	op = decodeOp(t, h.expect("POST", path+"/sync-batch", token, batch, 200))
	var changes struct {
		Cursor  string `json:"cursor"`
		Changes []struct {
			Deleted bool `json:"deleted"`
		} `json:"changes"`
	}
	if err := json.Unmarshal(h.expect("GET", path+"/changes?epoch="+op.Epoch+"&cursor="+initial.Revision, token, nil, 200), &changes); err != nil || changes.Cursor != op.Revision || len(changes.Changes) != 2 {
		t.Fatal("bad cursor", err)
	}
	for _, c := range changes.Changes {
		if !c.Deleted {
			t.Fatal("missing tombstone")
		}
	}
	h.expect("GET", path+"/changes?epoch="+store.UUID()+"&cursor="+initial.Revision, token, nil, 409)
	var history int
	if err := h.db.DB.QueryRow("SELECT count(*) FROM sync_history").Scan(&history); err != nil || history != 3 {
		t.Fatal("missing history", history, err)
	}
}
