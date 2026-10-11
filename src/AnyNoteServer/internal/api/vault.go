package api

import (
	"anynote/server/internal/protocol"
	"anynote/server/internal/store"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
)

func (s *Server) vault(w http.ResponseWriter, r *http.Request, id, path string, p store.Principal) error {
	parts := strings.Split(path, "/")
	query := r.URL.Query()
	if path == "capabilities" && r.Method == "GET" {
		respond(w, id, 200, map[string]any{"protocol_version": 1, "schema_version": 1, "realtime_version": 1, "limits": map[string]any{
			"nodes": s.Limits.Nodes, "depth": s.Limits.Depth, "node_bytes": s.Limits.NodeBytes, "vault_bytes": s.Limits.VaultBytes,
			"json_bytes": s.Limits.JSONBytes, "account_vaults": s.Store.AccountVaults, "account_bytes": strconv.FormatInt(s.Store.AccountBytes, 10)}})
		return nil
	}
	if path == "vaults" && r.Method == "GET" {
		cursor := query.Get("cursor")
		if cursor != "" && !protocol.UID(cursor) {
			return errors.New("INVALID_REQUEST")
		}
		limit := 100
		var err error
		if text := query.Get("limit"); text != "" {
			limit, err = strconv.Atoi(text)
			if err != nil || limit < 1 || limit > 100 {
				return errors.New("INVALID_REQUEST")
			}
		}
		result, err := s.Store.List(r.Context(), p, cursor, limit)
		if err != nil {
			return err
		}
		respond(w, id, 200, result)
		return nil
	}
	if len(parts) == 2 && parts[0] == "operations" && r.Method == "GET" {
		if !protocol.UUID(parts[1]) {
			return errors.New("INVALID_REQUEST")
		}
		result, err := s.Store.Operation(r.Context(), p, parts[1])
		if err != nil {
			return err
		}
		respond(w, id, 200, result)
		return nil
	}
	if len(parts) < 2 || parts[0] != "vaults" || !protocol.UID(parts[1]) {
		return store.ErrNotFound
	}
	uid := parts[1]
	if len(parts) == 3 && r.Method == "GET" && (parts[2] == "sync-state" || parts[2] == "sync-manifest") {
		result, err := s.Store.SyncState(r.Context(), p, uid, parts[2] == "sync-manifest")
		if err != nil {
			return err
		}
		respond(w, id, 200, result)
		return nil
	}
	if len(parts) == 3 && parts[2] == "changes" && r.Method == "GET" {
		result, err := s.Store.Changes(r.Context(), p, uid, query.Get("epoch"), query.Get("cursor"))
		if err != nil {
			return err
		}
		respond(w, id, 200, result)
		return nil
	}
	if len(parts) == 5 && parts[2] == "nodes" && parts[4] == "content" && protocol.UUID(parts[3]) && r.Method == "GET" {
		result, err := s.Store.ReadOne(r.Context(), p, uid, parts[3], query.Get("epoch"), query.Get("revision"))
		if err != nil {
			return err
		}
		respond(w, id, 200, result)
		return nil
	}
	if len(parts) == 3 && parts[2] == "sync-batch" && r.Method == "POST" {
		r.Body = http.MaxBytesReader(w, r.Body, int64(s.Limits.JSONBytes))
		batch, err := protocol.ReadBatch(r.Body, s.Limits)
		if err != nil {
			return err
		}
		result, err := s.Store.SyncBatch(r.Context(), p, uid, batch, s.Limits)
		if err != nil {
			return err
		}
		respond(w, id, 200, result)
		return nil
	}
	root, kind := "", ""
	if len(parts) == 2 {
		kind = "delete"
	} else if len(parts) == 3 && (parts[2] == "manifest" || parts[2] == "snapshot" || parts[2] == "remark") {
		kind = parts[2]
	} else if len(parts) == 5 && parts[2] == "nodes" && parts[4] == "subtree" && protocol.UUID(parts[3]) {
		kind = "subtree"
		root = parts[3]
	} else {
		return store.ErrNotFound
	}
	if r.Method == "GET" && (kind == "manifest" || kind == "snapshot" || kind == "subtree") {
		epoch, revision := query.Get("epoch"), query.Get("revision")
		if kind != "manifest" || epoch != "" || revision != "" {
			if !protocol.UUID(epoch) || !protocol.Positive64(revision) {
				return errors.New("INVALID_PRECONDITION")
			}
		}
		vault, snapshot, err := s.Store.Read(r.Context(), p, uid, epoch, revision)
		if err != nil {
			return err
		}
		if kind == "manifest" {
			nodes := []map[string]any{}
			for _, n := range snapshot.Nodes {
				nodes = append(nodes, map[string]any{"sync_id": n.SyncID, "source_id": n.SourceID, "parent_sync_id": n.ParentSyncID,
					"sequence": n.Sequence, "title": n.Title, "node_type": n.NodeType, "created_time": n.CreatedTime, "modified_time": n.ModifiedTime, "format_type": n.FormatType, "content_hash": n.ContentHash})
			}
			respond(w, id, 200, map[string]any{"vault": vault, "nodes": nodes})
			return nil
		}
		if kind == "subtree" {
			snapshot, err = protocol.Subtree(snapshot, root)
			if err != nil {
				return err
			}
		}
		// Database read transaction has completed before waiting on the network.
		encodedVault, err := json.Marshal(vault)
		if err != nil {
			return err
		}
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		w.Header().Set("Cache-Control", "no-store")
		key := "snapshot"
		extra := ""
		if kind == "subtree" {
			key = "source"
			extra = fmt.Sprintf(`,"root_sync_id":%q`, root)
		}
		if _, err = fmt.Fprintf(w, `{"request_id":%q,"data":{"vault":%s%s,%q:`, id, encodedVault, extra, key); err != nil {
			return nil
		}
		if err = protocol.WriteSnapshot(w, snapshot, s.Limits); err != nil {
			s.Log.Info("response interrupted", "request_id", id)
			return nil
		}
		_, _ = fmt.Fprint(w, "}}")
		return nil
	}
	if !((r.Method == "PUT" && (kind == "snapshot" || kind == "subtree")) || (r.Method == "PATCH" && kind == "remark") || (r.Method == "DELETE" && (kind == "delete" || kind == "subtree"))) {
		return errors.New("INVALID_REQUEST")
	}
	if kind == "subtree" && r.Method == "DELETE" {
		kind = "delete_subtree"
	}
	r.Body = http.MaxBytesReader(w, r.Body, int64(s.Limits.JSONBytes))
	request, err := protocol.ReadWriteRequest(r.Body, s.Limits, kind)
	if err != nil {
		return err
	}
	result, err := s.Store.Write(r.Context(), p, uid, root, kind, request, s.Limits)
	if err != nil {
		return err
	}
	respond(w, id, 200, result)
	return nil
}
