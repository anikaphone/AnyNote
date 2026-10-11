package store

import (
	"anynote/server/internal/protocol"
	"context"
	"database/sql"
	"encoding/base64"
	"encoding/json"
	"errors"
	"math"
	"sort"
	"strconv"
)

const realtimeSchema = `
CREATE TABLE IF NOT EXISTS sync_changes(account_id INTEGER NOT NULL,vault_uid TEXT NOT NULL,sync_id TEXT NOT NULL,revision INTEGER NOT NULL,deleted INTEGER NOT NULL,PRIMARY KEY(account_id,vault_uid,sync_id));
CREATE TABLE IF NOT EXISTS sync_history(id INTEGER PRIMARY KEY,account_id INTEGER,vault_uid TEXT,sync_id TEXT,node_json TEXT,rtf BLOB,saved_at TEXT);
CREATE INDEX IF NOT EXISTS sync_history_node ON sync_history(account_id,vault_uid,sync_id,id);
CREATE INDEX IF NOT EXISTS sync_changes_cursor ON sync_changes(account_id,vault_uid,revision);
CREATE TRIGGER IF NOT EXISTS sync_vault_delete AFTER DELETE ON vaults BEGIN
 DELETE FROM sync_changes WHERE account_id=old.account_id AND vault_uid=old.uid;
 DELETE FROM sync_history WHERE account_id=old.account_id AND vault_uid=old.uid; END;
CREATE TRIGGER IF NOT EXISTS sync_change_insert AFTER INSERT ON nodes BEGIN
 INSERT INTO sync_changes VALUES(new.account_id,new.vault_uid,new.sync_id,(SELECT revision FROM vaults WHERE account_id=new.account_id AND uid=new.vault_uid),0)
 ON CONFLICT(account_id,vault_uid,sync_id) DO UPDATE SET revision=excluded.revision,deleted=0; END;
CREATE TRIGGER IF NOT EXISTS sync_change_update AFTER UPDATE ON nodes BEGIN
 INSERT INTO sync_changes VALUES(new.account_id,new.vault_uid,new.sync_id,(SELECT revision FROM vaults WHERE account_id=new.account_id AND uid=new.vault_uid),0)
 ON CONFLICT(account_id,vault_uid,sync_id) DO UPDATE SET revision=excluded.revision,deleted=0; END;
CREATE TRIGGER IF NOT EXISTS sync_change_delete AFTER DELETE ON nodes BEGIN
 INSERT INTO sync_changes VALUES(old.account_id,old.vault_uid,old.sync_id,coalesce((SELECT revision FROM vaults WHERE account_id=old.account_id AND uid=old.vault_uid),0),1)
 ON CONFLICT(account_id,vault_uid,sync_id) DO UPDATE SET revision=excluded.revision,deleted=1; END;
`

// Metadata never loads or encodes RTF. Used for both initial comparison and polling.
func readMetadata(ctx context.Context, tx *sql.Tx, account int64, v Vault) (protocol.Snapshot, error) {
	s := protocol.Snapshot{ProtocolVersion: 1, SchemaVersion: 1, UID: v.UID, Name: v.Name, Nodes: []protocol.Node{}}
	rows, err := tx.QueryContext(ctx, `SELECT sync_id,remote_id,parent_sync_id,sequence,title,node_type,created_time,modified_time,format_type,content_hash FROM nodes WHERE account_id=? AND vault_uid=? ORDER BY sync_id`, account, v.UID)
	if err != nil {
		return s, err
	}
	defer rows.Close()
	for rows.Next() {
		var n protocol.Node
		var id, c, m int64
		if err = rows.Scan(&n.SyncID, &id, &n.ParentSyncID, &n.Sequence, &n.Title, &n.NodeType, &c, &m, &n.FormatType, &n.ContentHash); err != nil {
			return s, err
		}
		n.SourceID = strconv.FormatInt(id, 10)
		n.CreatedTime = strconv.FormatInt(c, 10)
		n.ModifiedTime = strconv.FormatInt(m, 10)
		s.Nodes = append(s.Nodes, n)
	}
	return s, rows.Err()
}
func (s *Store) SyncState(ctx context.Context, p Principal, uid string, full bool) (map[string]any, error) {
	tx, err := s.DB.BeginTx(ctx, &sql.TxOptions{ReadOnly: true})
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()
	if err = Authorize(ctx, tx, p); err != nil {
		return nil, err
	}
	v, err := readVault(ctx, tx, p.AccountID, uid)
	if err != nil {
		return nil, err
	}
	result := map[string]any{"vault": v, "cursor": v.Revision}
	if full {
		snapshot, e := readMetadata(ctx, tx, p.AccountID, v)
		if e != nil {
			return nil, e
		}
		nodes := []any{}
		for _, n := range snapshot.Nodes {
			m := metadata(n)
			m["source_id"] = n.SourceID
			nodes = append(nodes, m)
		}
		result["nodes"] = nodes
	}
	return result, tx.Commit()
}
func (s *Store) Changes(ctx context.Context, p Principal, uid, epoch, cursor string) (map[string]any, error) {
	if !protocol.UUID(epoch) || !protocol.Positive64(cursor) {
		return nil, errors.New("INVALID_PRECONDITION")
	}
	tx, err := s.DB.BeginTx(ctx, &sql.TxOptions{ReadOnly: true})
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()
	if err = Authorize(ctx, tx, p); err != nil {
		return nil, err
	}
	v, err := readVault(ctx, tx, p.AccountID, uid)
	if err != nil {
		return nil, err
	}
	from, _ := strconv.ParseInt(cursor, 10, 64)
	head, _ := strconv.ParseInt(v.Revision, 10, 64)
	if v.Epoch != epoch || from > head {
		return nil, errors.New("VERSION_CONFLICT")
	}
	rows, err := tx.QueryContext(ctx, "SELECT sync_id,deleted FROM sync_changes WHERE account_id=? AND vault_uid=? AND revision>? ORDER BY sync_id LIMIT 20001", p.AccountID, uid, from)
	if err != nil {
		return nil, err
	}
	changes := []any{}
	for rows.Next() {
		var id string
		var deleted int
		if err = rows.Scan(&id, &deleted); err != nil {
			rows.Close()
			return nil, err
		}
		changes = append(changes, map[string]any{"sync_id": id, "deleted": deleted != 0})
		if len(changes) > 20000 {
			rows.Close()
			return nil, errors.New("CURSOR_EXPIRED")
		}
	}
	err = rows.Err()
	rows.Close()
	if err != nil {
		return nil, err
	}
	snapshot, err := readMetadata(ctx, tx, p.AccountID, v)
	if err != nil {
		return nil, err
	}
	index := map[string]protocol.Node{}
	for _, n := range snapshot.Nodes {
		index[n.SyncID] = n
	}
	for _, item := range changes {
		change := item.(map[string]any)
		if !change["deleted"].(bool) {
			n, ok := index[change["sync_id"].(string)]
			if !ok {
				return nil, errors.New("INVALID_NODE")
			}
			change["node"] = metadata(n)
		}
	}
	return map[string]any{"vault": v, "cursor": v.Revision, "changes": changes}, tx.Commit()
}
func (s *Store) ReadOne(ctx context.Context, p Principal, uid, id, epoch, revision string) (protocol.Node, error) {
	var n protocol.Node
	tx, err := s.DB.BeginTx(ctx, &sql.TxOptions{ReadOnly: true})
	if err != nil {
		return n, err
	}
	defer tx.Rollback()
	if err = Authorize(ctx, tx, p); err != nil {
		return n, err
	}
	v, err := readVault(ctx, tx, p.AccountID, uid)
	if err != nil {
		return n, err
	}
	if v.Epoch != epoch || v.Revision != revision {
		return n, errors.New("VERSION_CONFLICT")
	}
	var source, c, m int64
	err = tx.QueryRowContext(ctx, `SELECT sync_id,remote_id,parent_sync_id,sequence,title,node_type,created_time,modified_time,format_type,content_hash,rtf FROM nodes WHERE account_id=? AND vault_uid=? AND sync_id=?`, p.AccountID, uid, id).Scan(&n.SyncID, &source, &n.ParentSyncID, &n.Sequence, &n.Title, &n.NodeType, &c, &m, &n.FormatType, &n.ContentHash, &n.RTF)
	if errors.Is(err, sql.ErrNoRows) {
		return n, ErrNotFound
	}
	if err != nil {
		return n, err
	}
	n.SourceID = strconv.FormatInt(source, 10)
	n.CreatedTime = strconv.FormatInt(c, 10)
	n.ModifiedTime = strconv.FormatInt(m, 10)
	n.RTFBase64 = base64.StdEncoding.EncodeToString(n.RTF)
	return n, tx.Commit()
}

func (s *Store) SyncBatch(ctx context.Context, p Principal, uid string, b protocol.Batch, limits protocol.Limits) (Operation, error) {
	var result Operation
	b.Upserts = append([]protocol.Node{}, b.Upserts...)
	b.Deletes = append([]string{}, b.Deletes...)
	if !protocol.UID(uid) || !protocol.UUID(b.Precondition.OperationID) || !protocol.UUID(b.Precondition.Epoch) || !protocol.Positive64(b.Precondition.Revision) || b.Precondition.CreateOnly {
		return result, errors.New("INVALID_PRECONDITION")
	}
	if len(b.Upserts) > limits.Nodes || len(b.Deletes) > limits.Nodes {
		return result, errors.New("LIMIT_EXCEEDED")
	}
	seen := map[string]bool{}
	total := 0
	for i := range b.Upserts {
		n := &b.Upserts[i]
		n.RTFBase64 = base64.StdEncoding.EncodeToString(n.RTF)
		if err := protocol.ValidateNode(n, limits); err != nil {
			return result, err
		}
		n.RTFBase64 = ""
		if seen[n.SyncID] {
			return result, errors.New("DUPLICATE_SYNC_ID")
		}
		seen[n.SyncID] = true
		total += len(n.RTF)
	}
	if total > limits.VaultBytes {
		return result, errors.New("LIMIT_EXCEEDED")
	}
	for _, id := range b.Deletes {
		if !protocol.UUID(id) || seen[id] {
			return result, errors.New("INVALID_UUID")
		}
		seen[id] = true
	}
	descriptor := map[string]any{"action": "sync-batch", "uid": uid, "epoch": b.Precondition.Epoch, "revision": b.Precondition.Revision, "deletes": b.Deletes}
	sort.Slice(b.Upserts, func(i, j int) bool { return b.Upserts[i].SyncID < b.Upserts[j].SyncID })
	sort.Strings(b.Deletes)
	deleted := []any{}
	for _, id := range b.Deletes {
		deleted = append(deleted, id)
	}
	descriptor["deletes"] = deleted
	nodes := []any{}
	for _, n := range b.Upserts {
		nodes = append(nodes, metadata(n))
	}
	descriptor["upserts"] = nodes
	hash, err := protocol.RequestHash(descriptor)
	if err != nil {
		return result, err
	}
	err = s.transaction(ctx, func(tx *sql.Tx) error {
		if err := Authorize(ctx, tx, p); err != nil {
			return err
		}
		var previous, raw string
		err := tx.QueryRowContext(ctx, "SELECT request_hash,result FROM operations WHERE account_id=? AND operation_id=?", p.AccountID, b.Precondition.OperationID).Scan(&previous, &raw)
		if err == nil {
			if previous != hash {
				return errors.New("OPERATION_ID_REUSED")
			}
			return json.Unmarshal([]byte(raw), &result)
		}
		if !errors.Is(err, sql.ErrNoRows) {
			return err
		}
		v, err := readVault(ctx, tx, p.AccountID, uid)
		if err != nil {
			return err
		}
		if v.Epoch != b.Precondition.Epoch || v.Revision != b.Precondition.Revision {
			return errors.New("VERSION_CONFLICT")
		}
		revision, _ := strconv.ParseInt(v.Revision, 10, 64)
		if revision == math.MaxInt64 {
			return errors.New("REVISION_EXHAUSTED")
		}
		revision++
		old, err := readMetadata(ctx, tx, p.AccountID, v)
		if err != nil {
			return err
		}
		index := map[string]protocol.Node{}
		var maxID int64
		for _, n := range old.Nodes {
			index[n.SyncID] = n
			id, _ := strconv.ParseInt(n.SourceID, 10, 64)
			maxID = max(maxID, id)
		}
		result = Operation{OperationID: b.Precondition.OperationID, Status: "committed", UID: uid, Epoch: v.Epoch, Revision: strconv.FormatInt(revision, 10)}
		for _, id := range b.Deletes {
			if _, ok := index[id]; ok {
				result.Deleted++
			}
			delete(index, id)
		}
		for i, n := range b.Upserts {
			if previous, ok := index[n.SyncID]; ok {
				n.SourceID = previous.SourceID
				result.Updated++
			} else {
				if maxID == math.MaxInt64 {
					return errors.New("ID_EXHAUSTED")
				}
				maxID++
				n.SourceID = strconv.FormatInt(maxID, 10)
				result.Created++
			}
			index[n.SyncID] = n
			b.Upserts[i] = n
		}
		final := []protocol.Node{}
		for _, n := range index {
			final = append(final, n)
		}
		if err = protocol.ValidateTree(final, limits); err != nil {
			return err
		}
		// Save overwritten rows before changing the revision, in the same transaction.
		affected := append([]string{}, b.Deletes...)
		for _, n := range b.Upserts {
			affected = append(affected, n.SyncID)
		}
		for _, id := range affected {
			if _, err = tx.ExecContext(ctx, `INSERT INTO sync_history(account_id,vault_uid,sync_id,node_json,rtf,saved_at) SELECT account_id,vault_uid,sync_id,json_object('title',title,'parent_sync_id',parent_sync_id,'sequence',sequence,'created_time',created_time,'modified_time',modified_time,'content_hash',content_hash),rtf,? FROM nodes WHERE account_id=? AND vault_uid=? AND sync_id=?`, timestamp(), p.AccountID, uid, id); err != nil {
				return err
			}
			if _, err = tx.ExecContext(ctx, `DELETE FROM sync_history WHERE account_id=? AND vault_uid=? AND sync_id=? AND id NOT IN (SELECT id FROM sync_history WHERE account_id=? AND vault_uid=? AND sync_id=? ORDER BY id DESC LIMIT 10)`, p.AccountID, uid, id, p.AccountID, uid, id); err != nil {
				return err
			}
		}
		if _, err = tx.ExecContext(ctx, "UPDATE vaults SET revision=?,updated_at=? WHERE account_id=? AND uid=?", revision, timestamp(), p.AccountID, uid); err != nil {
			return err
		}
		for _, id := range b.Deletes {
			if _, err = tx.ExecContext(ctx, "DELETE FROM nodes WHERE account_id=? AND vault_uid=? AND sync_id=?", p.AccountID, uid, id); err != nil {
				return err
			}
		}
		for _, n := range b.Upserts {
			content := n.RTF
			if content == nil {
				content = []byte{}
			}
			if _, err = tx.ExecContext(ctx, `INSERT INTO nodes VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(account_id,vault_uid,sync_id) DO UPDATE SET parent_sync_id=excluded.parent_sync_id,sequence=excluded.sequence,title=excluded.title,node_type=excluded.node_type,created_time=excluded.created_time,modified_time=excluded.modified_time,format_type=excluded.format_type,rtf=excluded.rtf,content_hash=excluded.content_hash`, p.AccountID, uid, n.SyncID, n.SourceID, n.ParentSyncID, n.Sequence, n.Title, n.NodeType, n.CreatedTime, n.ModifiedTime, n.FormatType, content, n.ContentHash); err != nil {
				return err
			}
		}
		if _, err = tx.ExecContext(ctx, `DELETE FROM sync_history WHERE id IN (SELECT id FROM (SELECT id,sum(length(rtf)+length(node_json)) OVER (ORDER BY id DESC) AS bytes FROM sync_history WHERE account_id=?) WHERE bytes>268435456)`, p.AccountID); err != nil {
			return err
		}
		var used, vaultBytes int64
		if err = tx.QueryRowContext(ctx, "SELECT coalesce(sum(length(rtf)),0) FROM nodes WHERE account_id=?", p.AccountID).Scan(&used); err != nil {
			return err
		}
		if err = tx.QueryRowContext(ctx, "SELECT coalesce(sum(length(rtf)),0) FROM nodes WHERE account_id=? AND vault_uid=?", p.AccountID, uid).Scan(&vaultBytes); err != nil {
			return err
		}
		if used > s.AccountBytes || vaultBytes > int64(limits.VaultBytes) {
			return ErrQuota
		}
		encoded, err := json.Marshal(result)
		if err != nil {
			return err
		}
		_, err = tx.ExecContext(ctx, "INSERT INTO operations VALUES(?,?,?,?,?)", p.AccountID, result.OperationID, hash, string(encoded), timestamp())
		return err
	})
	return result, err
}
