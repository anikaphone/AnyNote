package store

import (
	"anynote/server/internal/protocol"
	"bytes"
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"io"
	"math"
	"reflect"
	"sort"
	"strconv"
)

type Operation struct {
	OperationID string `json:"operation_id"`
	Status      string `json:"status"`
	UID         string `json:"uid"`
	Epoch       string `json:"epoch"`
	Revision    string `json:"revision"`
	Created     int    `json:"created"`
	Updated     int    `json:"updated"`
	Deleted     int    `json:"deleted"`
}

func metadata(n protocol.Node) map[string]any {
	var p any
	if n.ParentSyncID != nil {
		p = *n.ParentSyncID
	}
	return map[string]any{"sync_id": n.SyncID, "parent_sync_id": p, "sequence": int(n.Sequence), "title": n.Title, "node_type": n.NodeType,
		"created_time": n.CreatedTime, "modified_time": n.ModifiedTime, "format_type": n.FormatType, "content_hash": n.ContentHash}
}
func requestHash(uid, root, kind string, r protocol.WriteRequest) (string, error) {
	d := map[string]any{"uid": uid, "root_sync_id": root, "action": kind, "expected_epoch": r.Precondition.Epoch,
		"expected_revision": r.Precondition.Revision, "create_only": r.Precondition.CreateOnly}
	if kind == "remark" {
		d["remark"] = r.Remark
	}
	if kind == "snapshot" || kind == "subtree" {
		d["name"] = r.Snapshot.Name
		nodes := append([]protocol.Node{}, r.Snapshot.Nodes...)
		sort.Slice(nodes, func(i, j int) bool { return nodes[i].SyncID < nodes[j].SyncID })
		list := []any{}
		for _, n := range nodes {
			list = append(list, metadata(n))
		}
		d["nodes"] = list
	}
	return protocol.RequestHash(d)
}
func (s *Store) Operation(ctx context.Context, p Principal, id string) (Operation, error) {
	var op Operation
	var raw string
	err := s.DB.QueryRowContext(ctx, "SELECT result FROM operations WHERE account_id=? AND operation_id=?", p.AccountID, id).Scan(&raw)
	if errors.Is(err, sql.ErrNoRows) {
		return op, ErrNotFound
	}
	if err != nil {
		return op, err
	}
	err = json.Unmarshal([]byte(raw), &op)
	return op, err
}
func (s *Store) Write(ctx context.Context, p Principal, uid, root, kind string, r protocol.WriteRequest, limits protocol.Limits) (Operation, error) {
	var result Operation
	if kind == "snapshot" || kind == "subtree" {
		if r.Snapshot.UID != uid {
			return result, errors.New("UID_MISMATCH")
		}
		if err := protocol.WriteSnapshot(io.Discard, r.Snapshot, limits); err != nil {
			return result, err
		}
	}
	hash, err := requestHash(uid, root, kind, r)
	if err != nil {
		return result, err
	}
	err = s.transaction(ctx, func(tx *sql.Tx) error {
		if err := Authorize(ctx, tx, p); err != nil {
			return err
		}
		var priorHash, raw string
		err := tx.QueryRowContext(ctx, "SELECT request_hash,result FROM operations WHERE account_id=? AND operation_id=?", p.AccountID, r.Precondition.OperationID).Scan(&priorHash, &raw)
		if err == nil {
			if priorHash != hash {
				return errors.New("OPERATION_ID_REUSED")
			}
			return json.Unmarshal([]byte(raw), &result)
		}
		if !errors.Is(err, sql.ErrNoRows) {
			return err
		}
		vault, err := readVault(ctx, tx, p.AccountID, uid)
		exists := err == nil
		if err != nil && !errors.Is(err, ErrNotFound) {
			return err
		}
		if r.Precondition.CreateOnly {
			if kind != "snapshot" || exists {
				return errors.New("VERSION_CONFLICT")
			}
		} else {
			if !exists {
				return ErrNotFound
			}
			if vault.Epoch != r.Precondition.Epoch || vault.Revision != r.Precondition.Revision {
				return errors.New("VERSION_CONFLICT")
			}
		}
		now := timestamp()
		revision := int64(1)
		old := protocol.Snapshot{ProtocolVersion: 1, SchemaVersion: 1, UID: uid, Nodes: []protocol.Node{}}
		if exists {
			revision, err = strconv.ParseInt(vault.Revision, 10, 64)
			if err != nil {
				return err
			}
			if revision == math.MaxInt64 {
				return errors.New("REVISION_EXHAUSTED")
			}
			revision++
			old, err = readNodes(ctx, tx, p.AccountID, vault)
			if err != nil {
				return err
			}
		} else {
			vault = Vault{UID: uid, Epoch: UUID(), Name: r.Snapshot.Name, CreatedAt: now}
		}
		next := old
		switch kind {
		case "snapshot":
			next = r.Snapshot
			vault.Name = next.Name
		case "subtree":
			next, err = protocol.PlanSubtree(old, r.Snapshot, root, limits)
			if err != nil {
				return err
			}
		case "delete_subtree":
			found := false
			for _, n := range old.Nodes {
				if n.SyncID == root {
					found = true
					break
				}
			}
			if !found {
				return ErrNotFound
			}
			deleted := protocol.Descendants(old.Nodes, root)
			next.Nodes = []protocol.Node{}
			for _, n := range old.Nodes {
				if !deleted[n.SyncID] {
					next.Nodes = append(next.Nodes, n)
				}
			}
		case "remark":
			vault.Remark = r.Remark
		case "delete":
			next.Nodes = []protocol.Node{}
		default:
			return errors.New("INVALID_REQUEST")
		}
		if kind != "delete" {
			if err = protocol.WriteSnapshot(io.Discard, next, limits); err != nil {
				return err
			}
		}
		result = Operation{OperationID: r.Precondition.OperationID, Status: "committed", UID: uid, Epoch: vault.Epoch, Revision: strconv.FormatInt(revision, 10)}
		oldIndex := map[string]protocol.Node{}
		nextIndex := map[string]bool{}
		var maxID int64
		for _, n := range old.Nodes {
			oldIndex[n.SyncID] = n
			id, _ := strconv.ParseInt(n.SourceID, 10, 64)
			maxID = max(maxID, id)
		}
		for i, n := range next.Nodes {
			nextIndex[n.SyncID] = true
			if previous, ok := oldIndex[n.SyncID]; ok {
				next.Nodes[i].SourceID = previous.SourceID
				if !reflect.DeepEqual(metadata(n), metadata(previous)) {
					result.Updated++
				}
			} else {
				if maxID == math.MaxInt64 {
					return errors.New("ID_EXHAUSTED")
				}
				maxID++
				next.Nodes[i].SourceID = strconv.FormatInt(maxID, 10)
				result.Created++
			}
		}
		for id := range oldIndex {
			if !nextIndex[id] {
				result.Deleted++
			}
		}
		if kind == "delete" {
			if _, err = tx.ExecContext(ctx, "DELETE FROM vaults WHERE account_id=? AND uid=?", p.AccountID, uid); err != nil {
				return err
			}
		} else {
			var vaultCount int
			var used int64
			if err = tx.QueryRowContext(ctx, "SELECT count(*) FROM vaults WHERE account_id=? AND uid<>?", p.AccountID, uid).Scan(&vaultCount); err != nil {
				return err
			}
			if err = tx.QueryRowContext(ctx, "SELECT coalesce(sum(length(rtf)),0) FROM nodes WHERE account_id=? AND vault_uid<>?", p.AccountID, uid).Scan(&used); err != nil {
				return err
			}
			for _, n := range next.Nodes {
				used += int64(len(n.RTF))
			}
			if vaultCount >= s.AccountVaults || used > s.AccountBytes {
				return ErrQuota
			}
			if _, err = tx.ExecContext(ctx, `INSERT INTO vaults(account_id,uid,epoch,revision,name,remark,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?)
    ON CONFLICT(account_id,uid) DO UPDATE SET revision=excluded.revision,name=excluded.name,remark=excluded.remark,updated_at=excluded.updated_at`,
				p.AccountID, uid, vault.Epoch, revision, vault.Name, vault.Remark, vault.CreatedAt, now); err != nil {
				return err
			}
			if kind != "remark" {
				if _, err = tx.ExecContext(ctx, "DELETE FROM nodes WHERE account_id=? AND vault_uid=?", p.AccountID, uid); err != nil {
					return err
				}
				statement, err := tx.PrepareContext(ctx, "INSERT INTO nodes VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)")
				if err != nil {
					return err
				}
				defer statement.Close()
				for _, n := range next.Nodes {
					id, _ := strconv.ParseInt(n.SourceID, 10, 64)
					created, _ := strconv.ParseInt(n.CreatedTime, 10, 64)
					modified, _ := strconv.ParseInt(n.ModifiedTime, 10, 64)
					content := n.RTF
					if content == nil {
						content = []byte{}
					}
					if _, err = statement.ExecContext(ctx, p.AccountID, uid, n.SyncID, id, n.ParentSyncID, n.Sequence, n.Title, n.NodeType, created, modified, n.FormatType, content, n.ContentHash); err != nil {
						return err
					}
				}
			}
		}
		encoded, err := json.Marshal(result)
		if err != nil {
			return err
		}
		_, err = tx.ExecContext(ctx, "INSERT INTO operations VALUES(?,?,?,?,?)", p.AccountID, result.OperationID, hash, string(encoded), now)
		return err
	})
	return result, err
}

// DecodeDescriptor is also used by integration fixtures to retain exact integers.
func DecodeDescriptor(raw []byte) (any, error) {
	var value any
	d := json.NewDecoder(bytes.NewReader(raw))
	d.UseNumber()
	err := d.Decode(&value)
	return value, err
}
